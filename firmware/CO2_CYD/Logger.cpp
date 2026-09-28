#include "Logger.h"

#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ctype.h>
#include <time.h>

#include "BoardPins.h"
#include "Config.h"
#include "LgfxCyd.h"

namespace {
// Own SPI instance for the card, bound to SPI3 on the SD pin set.
SPIClass g_sdSpi(VSPI);

// Conservative: the CYD's SD traces are unshielded and share a host that is
// being re-routed. Logging one short line every ten seconds does not need
// speed, and a card that enumerates reliably is worth more than throughput.
constexpr uint32_t SD_FREQ_HZ = 10000000UL;
}  // namespace

bool Logger::borrowBus() {
  g_sdSpi.begin(pins::SD_SCK, pins::SD_MISO, pins::SD_MOSI, pins::SD_CS);
  return true;
}

void Logger::returnBus() {
  g_sdSpi.end();
  // Point SPI3 back at the XPT2046 before anything can poll it again.
  if (_tft) _tft->reinitTouch();
}

bool Logger::begin(LgfxCyd* tft) {
  _tft = tft;

  borrowBus();
  const bool ok = SD.begin(pins::SD_CS, g_sdSpi, SD_FREQ_HZ);
  if (ok) {
    const uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
      SD.end();
      _lastError = "no card";
    } else {
      _card = true;
      _enabled = openNewFile();
      if (!_enabled) _lastError = "cannot open log file";
    }
  } else {
    _lastError = "SD.begin failed";
  }
  returnBus();

  return _enabled;
}

// Next boot number from NVS, a fresh file with the v2 header. The counter is
// committed only after the file exists, so a card that refuses the open does
// not burn a number. Bus must already be borrowed.
bool Logger::openNewFile() {
  Preferences p;
  uint32_t n = 1;
  const bool nvs = p.begin("log", false);
  if (nvs) { n = p.getUInt("boot", 0) + 1; if (n > 9999) n = 1; }

  snprintf(_file, sizeof(_file), "%s%04lu.csv", config::LOG_PREFIX, (unsigned long)n);
  File f = SD.open(_file, FILE_WRITE);
  if (!f) { _file[0] = '\0'; if (nvs) p.end(); return false; }
  // co2_ppm / co2_pct are corrected (config::CO2_CAL_*); raw_ppm is the same
  // rolling mean before correction, and cal_gain records what was applied.
  f.println(F("time_utc,uptime_s,co2_ppm,co2_pct,temp_c,pressure_hpa,"
              "pwm_period_ms,th_ms,tl_ms,status,raw_ppm,cal_gain"));
  f.close();

  if (nvs) { p.putUInt("boot", n); p.end(); }
  _boot = n;
  return true;
}

void Logger::update(const Instrument& inst) {
  if (!_enabled) return;

  const uint32_t now = millis();
  if (now < _nextMs) return;
  _nextMs = now + config::LOG_INTERVAL_MS;

  const Co2Pwm::Reading&       c = inst.co2();
  const Ds18b20Sensor::Reading& b = inst.probe();

  // Build the whole line before touching the bus, so the borrow window is as
  // short as possible.
  char line[192];
  char ppm[16], pct[16], raw[16], temp[16], pres[16];

  if (inst.haveReading()) {
    snprintf(ppm, sizeof(ppm), "%.0f", inst.displayPpm());
    snprintf(pct, sizeof(pct), "%.4f", inst.displayPercent());
    snprintf(raw, sizeof(raw), "%.0f", inst.rawAveragePpm());
  } else {
    // Empty rather than 0: a spreadsheet must not read a dropout as a
    // measurement of zero.
    ppm[0] = '\0';
    pct[0] = '\0';
    raw[0] = '\0';
  }
  // pressure_hpa stays in the schema for compatibility but is always empty:
  // the DS18B20 has no barometer.
  if (b.valid) snprintf(temp, sizeof(temp), "%.2f", b.temperatureC); else temp[0] = '\0';
  pres[0] = '\0';

  // Wall-clock only once NTP has set it; before that the column is empty and
  // uptime_s is the only time axis, which is at least never wrong.
  char stamp[24] = "";
  const time_t wall = time(nullptr);
  if (wall > 1600000000) {
    struct tm tmv;
    gmtime_r(&wall, &tmv);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  }

  snprintf(line, sizeof(line), "%s,%lu,%s,%s,%s,%s,%.1f,%.1f,%.1f,%s,%s,%.4f",
           stamp, (unsigned long)inst.uptimeSec(), ppm, pct, temp, pres,
           c.periodMs, c.thMs, c.tlMs, Instrument::statusText(inst.status()),
           raw, config::CO2_CAL_GAIN);

  borrowBus();
  File f = SD.open(_file, FILE_APPEND);
  if (f) {
    f.println(line);
    f.close();
    _rows++;
  } else {
    // A card pulled mid-session must not wedge the instrument; stop logging
    // and carry on measuring.
    _enabled = false;
    _lastError = "write failed, logging disabled";
  }
  returnBus();
}

// ------------------------------------------------------ web file access ----

bool Logger::validName(const char* name) {
  // co2_ 0 0 0 7 . c s v  -> 12 chars
  if (!name || strlen(name) != 12) return false;
  if (strncmp(name, "co2_", 4) != 0) return false;
  for (int i = 4; i < 8; i++) if (!isdigit((unsigned char)name[i])) return false;
  return strcmp(name + 8, ".csv") == 0;
}

size_t Logger::listFiles(FileInfo* out, size_t max) {
  if (!_card || !out || max == 0) return 0;
  size_t n = 0;
  borrowBus();
  File root = SD.open("/");
  if (root) {
    for (File f = root.openNextFile(); f && n < max; f = root.openNextFile()) {
      const char* nm = f.name();           // core 3.x: basename
      if (!f.isDirectory() && validName(nm)) {
        strncpy(out[n].name, nm, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = '\0';
        out[n].size = f.size();
        n++;
      }
      f.close();
    }
    root.close();
  }
  returnBus();
  return n;
}

bool Logger::streamFile(WebServer& server, const char* name) {
  if (!_card || !validName(name)) return false;
  char path[20];
  snprintf(path, sizeof(path), "/%s", name);

  borrowBus();
  File f = SD.open(path, FILE_READ);
  if (!f) { returnBus(); return false; }
  server.sendHeader("Content-Disposition", String("attachment; filename=") + name);
  server.streamFile(f, "text/csv");
  f.close();
  returnBus();
  return true;
}
