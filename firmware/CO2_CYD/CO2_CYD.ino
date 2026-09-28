//
// CO2_CYD -- incubator CO2 monitor on an ESP32-2432S028R "CYD".
//
// A small fixed-function lab instrument: it measures CO2 continuously, shows
// it large enough to read across a room, keeps a rolling history, and says
// plainly when it cannot measure rather than showing a stale number.
//
// -------------------------------------------------------------------------
// Hardware decisions that are not guessable from the code alone
// -------------------------------------------------------------------------
//  * MH-Z16 is read over PWM on GPIO35. GPIO35 is input-only, which is exactly
//    what a pulse-width measurement needs and makes it the cheapest pin on the
//    board to spend here. It must never be configured as an output.
//  * MH-Z16 full scale is 100000 ppm -- this is the 0-10 % VOL variant. The
//    datasheet's worked example uses 2000; using that constant reads 20x low.
//  * Temperature is a DS18B20 on GPIO27 (1-Wire, one pin). GPIO22 is unused:
//    its CN1 contact measured as an open line, which is what retired the
//    two-pin I2C BMP280 in the first place.
//  * The panel is on HSPI/SPI2; touch and the SD slot share VSPI/SPI3 but on
//    different pins, so the display never contends with either.
//
// Build:  ./build.sh flash        (FQBN esp32:esp32:jczn_2432s028r)
//
#include "BoardPins.h"
#include "Config.h"
#include "Instrument.h"
#include "Logger.h"
#include "NetLink.h"
#include "LgfxCyd.h"
#include "Screens.h"
#include "Theme.h"

static LgfxCyd    tft;
static Instrument instrument;
static Screens    screens;
static Logger     logger;
static NetLink    net;

enum class AppState : uint8_t { Booting, Running };
static AppState appState = AppState::Booting;
static uint32_t bootStartMs = 0;

// Backlight level actually applied, and the level the ambient sensor wants.
static uint8_t g_curBright    = config::BRIGHT_MAX;
static uint8_t g_targetBright = config::BRIGHT_MAX;

// ---------------------------------------------------------------------------
// Onboard RGB LED. Common anode: LOW lights a channel. Deliberately left dark
// during normal operation -- a lab instrument that blinks at you all day is
// harder to trust, not easier. It is only lit for a hard sensor fault.
// ---------------------------------------------------------------------------
static void ledInit() {
  pinMode(pins::LED_R, OUTPUT);
  pinMode(pins::LED_G, OUTPUT);
  pinMode(pins::LED_B, OUTPUT);
  digitalWrite(pins::LED_R, HIGH);
  digitalWrite(pins::LED_G, HIGH);
  digitalWrite(pins::LED_B, HIGH);
}

static void ledFault(bool on) {
  static bool last = false;
  if (on == last) return;
  last = on;
  digitalWrite(pins::LED_R, on ? LOW : HIGH);
}

// ---------------------------------------------------------------------------

static void banner() {
  Serial.println();
  Serial.printf("%s %s  (build %s)\n", config::FIRMWARE_NAME,
                config::FIRMWARE_VERSION, config::BUILD_STAMP);
  Serial.printf("CO2  MH-Z16 PWM on GPIO%d, full scale %.0f ppm\n",
                pins::CO2_PWM, config::CO2_RANGE_PPM);
  Serial.printf("TEMP DS18B20 1-Wire on GPIO%d (GPIO22 unused)\n", pins::ONEWIRE_DATA);
  Serial.printf("Thresholds  low %.2f %%  target %.2f %%  high %.2f %%\n",
                config::CO2_LOW_PCT, config::CO2_TARGET_PCT, config::CO2_HIGH_PCT);
  Serial.println(F("-------------------------------------------------------"));
}

static void serialReport() {
  const Co2Pwm::Reading& c = instrument.co2();
  const Ds18b20Sensor::Reading& b = instrument.probe();

  Serial.printf("%6lus  ", (unsigned long)instrument.uptimeSec());
  if (instrument.haveReading()) {
    Serial.printf("%6.0f ppm  %.3f %%  ", instrument.displayPpm(),
                  instrument.displayPercent());
  } else {
    Serial.print(F("    -- ppm    --     "));
  }
  Serial.printf("[%-12s]  Th=%6.1f Tl=%6.1f T=%6.1f  e/s=%.1f  ok=%lu bad=%lu  age=%.1fs",
                Instrument::statusText(instrument.status()),
                c.thMs, c.tlMs, c.periodMs, c.edgesPerSec,
                (unsigned long)c.validCount, (unsigned long)c.rejectCount,
                c.ageMs / 1000.0f);
  if (b.valid) Serial.printf("  | probe %.2f C (age %.1fs)", b.temperatureC, b.ageMs / 1000.0f);
  else if (b.present) Serial.print(F("  | probe present, no valid data yet"));
  else Serial.print(F("  | no DS18B20"));
  Serial.printf("  | ldr=%u bl=%u", (unsigned)instrument.ambientRaw(), (unsigned)g_curBright);
  Serial.println();
}

// ---------------------------------------------------------------------------
// Backlight self-check.
//
// Reads GPIO21's output routing straight from the GPIO matrix, so this reports
// what the pin is actually connected to rather than what we believe we set.
// 256 means "plain GPIO" (the LEDC signal is NOT attached and the backlight is
// off unless the pin happens to be driven high); 71-86 are the LEDC signals.
// A framebuffer screenshot cannot see any of this, which is why it exists.
// ---------------------------------------------------------------------------
#include "soc/gpio_reg.h"
static void backlightSelfCheck(const char* when) {
  const uint32_t sel = REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + 4 * pins::TFT_BL) & 0x1FF;
  const bool ledc = (sel >= 71 && sel <= 86);
  const int  level = digitalRead(pins::TFT_BL);
  Serial.printf("backlight %s: GPIO%d out_sel=%lu (%s)  duty=%lu  pin=%d\n", when,
                pins::TFT_BL, (unsigned long)sel, ledc ? "LEDC attached" : "NOT LEDC -- backlight off",
                (unsigned long)ledcRead(pins::TFT_BL), level);
  if (!ledc) {
    // Fail safe: a plain high output is a fully lit backlight. Dimming is lost
    // until the next boot, but the instrument is readable.
    pinMode(pins::TFT_BL, OUTPUT);
    digitalWrite(pins::TFT_BL, HIGH);
    Serial.println(F("backlight: forced hard ON as a fallback"));
  }
}

// ---------------------------------------------------------------------------
// Backlight.
//
// Two jobs: a short cross-fade around page changes so a full repaint is never
// seen happening, and a slow ambient follow from the onboard LDR so the panel
// is not a floodlight in a dark room. The fade is the one deliberate blocking
// delay in the firmware -- 140 ms on a user gesture -- and the PWM capture is
// interrupt-driven, so nothing is lost during it.
// ---------------------------------------------------------------------------
static void fadeTo(uint8_t level, uint32_t ms) {
  const int steps = 12;
  const int from = g_curBright;
  for (int k = 1; k <= steps; k++) {
    g_curBright = (uint8_t)(from + (int)(level - from) * k / steps);
    tft.setBrightness(g_curBright);
    delay(ms / steps);
  }
}

// Called every pass; samples the LDR twice a second and eases toward the
// brightness it implies, one step per pass so the change is never visible.
static void serviceBacklight() {
  static uint32_t nextSample = 0;
  static float    ema = -1.0f;
  const uint32_t now = millis();

  if (config::AUTO_DIM && now >= nextSample) {
    nextSample = now + 500;
    const uint16_t raw = analogRead(pins::LDR);
    instrument.setAmbientRaw(raw);
    ema = (ema < 0) ? raw : ema * 0.8f + raw * 0.2f;

    float dark = (ema - config::LDR_RAW_BRIGHT) / float(config::LDR_RAW_DARK - config::LDR_RAW_BRIGHT);
    if (!config::LDR_DARK_IS_HIGH) dark = 1.0f - dark;
    if (dark < 0) dark = 0; if (dark > 1) dark = 1;
    g_targetBright = (uint8_t)(config::BRIGHT_MAX - dark * (config::BRIGHT_MAX - config::BRIGHT_MIN));
  }

  static uint32_t nextStep = 0;
  if (now >= nextStep && g_curBright != g_targetBright) {
    nextStep = now + 25;
    g_curBright += (g_targetBright > g_curBright) ? 1 : -1;
    tft.setBrightness(g_curBright);
  }
}

static void changePage(Page next) {
  fadeTo(30, config::FADE_MS / 2);
  screens.show(next);
  fadeTo(g_targetBright, config::FADE_MS / 2);
}

// ---------------------------------------------------------------------------
// Touch calibration.
//
// The XPT2046's raw ADC range differs from panel to panel, so nominal bounds
// put taps a few millimetres from the finger -- worst at the screen edges,
// which is exactly where the graph page's span buttons live. LovyanGFX's
// four-corner calibration fixes that; the eight resulting values are kept in
// NVS and applied at every boot. Run it once with the serial command `c`.
// ---------------------------------------------------------------------------
#include <Preferences.h>

static bool loadTouchCalibration() {
  Preferences p;
  if (!p.begin("touch", true)) return false;
  uint16_t cal[8];
  const bool ok = p.getBytes("cal", cal, sizeof(cal)) == sizeof(cal);
  p.end();
  if (!ok) return false;
  tft.setTouchCalibrate(cal);
  Serial.printf("touch: calibration loaded  [%u %u %u %u %u %u %u %u]\n",
                cal[0], cal[1], cal[2], cal[3], cal[4], cal[5], cal[6], cal[7]);
  return true;
}

// Blocks until the four targets have been tapped. Acquisition is interrupt
// driven and survives; this is a one-off bench step, not a runtime path.
static void runTouchCalibration() {
  Serial.println(F("touch: calibrating -- tap the centre of each target as it appears"));
  uint16_t cal[8];
  tft.fillScreen(theme::BG);
  tft.setFont(&fonts::DejaVu12);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(theme::TEXT, theme::BG);
  tft.drawString("Touch calibration", 160, 100);
  tft.setTextColor(theme::TEXT_DIM, theme::BG);
  tft.drawString("tap the centre of each target", 160, 124);
  tft.calibrateTouch(cal, theme::ACCENT, theme::BG, 14);

  Preferences p;
  if (p.begin("touch", false)) { p.putBytes("cal", cal, sizeof(cal)); p.end(); }
  Serial.printf("touch: stored  [%u %u %u %u %u %u %u %u]\n",
                cal[0], cal[1], cal[2], cal[3], cal[4], cal[5], cal[6], cal[7]);
  if (appState == AppState::Running) screens.show(screens.page());
}

// ---------------------------------------------------------------------------
// Serial debug commands. Not part of normal operation; they exist so the UI
// can be inspected and iterated from the bench without touching the panel.
//
//   S        dump the framebuffer as raw RGB565 (see tools/screenshot.py)
//   1 2 3    switch to MAIN / GRAPH / DIAGNOSTICS
//   b        replay the boot sequence
//   c        run the four-corner touch calibration and store it in NVS
//
// The screenshot reads the panel back over SPI and streams ~150 KB, which at
// 115200 baud blocks the loop for around 13 s. Acquisition survives that --
// the PWM capture is interrupt-driven -- but the rolling average loses a few
// cycles, so this is a debugging aid and never runs on its own.
// ---------------------------------------------------------------------------
static void dumpScreenshot() {
  constexpr int STRIP = 4;
  static uint16_t strip[320 * STRIP];
  const int w = tft.width(), h = tft.height();

  Serial.printf("SCREENSHOT %d %d\n", w, h);
  Serial.flush();
  for (int y = 0; y < h; y += STRIP) {
    tft.readRect(0, y, w, STRIP, strip);
    Serial.write(reinterpret_cast<const uint8_t*>(strip), sizeof(strip));
  }
  Serial.flush();
  Serial.println();
  Serial.println(F("END"));
}

// Single characters act immediately (the screenshot tool sends a bare 'S');
// anything else is buffered to a newline and parsed as a word command:
//
//   wifi <ssid> <password>    store credentials in NVS and connect
//   wifi clear                forget them and turn the radio off
//   wifi                      report link state
//
static void handleLine(char* line) {
  char* cmd = strtok(line, " ");
  if (!cmd) return;
  if (strcmp(cmd, "wifi") == 0) {
    char* a = strtok(nullptr, " ");
    char* b = strtok(nullptr, "");
    if (!a) {
      Serial.printf("wifi: status %s  ssid=\"%s\"  %s\n",
                    net.state() == NetLink::State::Online ? "online" :
                    net.state() == NetLink::State::AccessPoint ? "access point" :
                    net.state() == NetLink::State::Connecting ? "connecting" : "off",
                    net.ssid(), net.summary());
    } else if (strcmp(a, "clear") == 0) {
      net.clearCredentials();
      Serial.printf("wifi: cleared  (%s)\n", net.summary());
    } else {
      if (net.setCredentials(a, b ? b : "")) Serial.printf("wifi: stored \"%s\", connecting\n", a);
      else Serial.println(F("wifi: failed to store credentials"));
    }
  } else {
    Serial.printf("? unknown command \"%s\"\n", cmd);
  }
}

static void serviceSerialCommands() {
  static char line[128];
  static size_t len = 0;

  while (Serial.available()) {
    const int c = Serial.read();
    if (len == 0) {
      bool handled = true;
      switch (c) {
        case 'S': case 's': dumpScreenshot(); break;
        case '1': if (appState == AppState::Running) changePage(Page::Main); break;
        case '2': if (appState == AppState::Running) changePage(Page::Graph); break;
        case '3': if (appState == AppState::Running) changePage(Page::Diagnostics); break;
        case '4': if (appState == AppState::Running) changePage(Page::Connect); break;
        case 'b': case 'B':
          appState = AppState::Booting; bootStartMs = millis(); screens.bootReset(); break;
        case 'c': case 'C': runTouchCalibration(); break;
        case 'L': pinMode(pins::TFT_BL, OUTPUT); digitalWrite(pins::TFT_BL, HIGH);
                  Serial.println(F("backlight: hard ON")); break;
        case 'D': backlightSelfCheck("on request"); break;
        case '\r': case '\n': case ' ': break;
        default: handled = false; break;
      }
      if (handled) continue;
    }
    if (c == '\r' || c == '\n') {
      line[len] = '\0';
      if (len) handleLine(line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = (char)c;
    } else {
      len = 0;   // overlong line: drop it rather than act on a fragment
    }
  }
}

// Advances pages on a tap, unless the current screen claimed the touch for one
// of its own controls. Debounced, and edge-triggered so holding a finger down
// does not run through every page.
static void serviceTouch() {
  static uint32_t nextPoll = 0;
  static uint32_t lastActionMs = 0;
  static bool wasPressed = false;

  const uint32_t now = millis();
  if (now < nextPoll) return;
  nextPoll = now + config::TOUCH_POLL_MS;

  int32_t x = 0, y = 0;
  const bool pressed = tft.getTouch(&x, &y);

  if (pressed && !wasPressed && (now - lastActionMs) >= config::TOUCH_DEBOUNCE_MS) {
    lastActionMs = now;
    Serial.printf("touch x=%d y=%d\n", (int)x, (int)y);
    if (!screens.handleTouch(x, y)) {
      const Page next = (Page)(((uint8_t)screens.page() + 1) % (uint8_t)Page::COUNT);
      changePage(next);
      Serial.printf("page -> %d\n", (int)next);
    }
  }
  wasPressed = pressed;
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 1200;
  while (!Serial && millis() < deadline) delay(10);

  ledInit();
  banner();

  tft.init();
  tft.setRotation(1);           // landscape, 320x240
  tft.setBrightness(config::BRIGHT_MAX);
  backlightSelfCheck("after tft.init");
  analogReadResolution(12);
  pinMode(pins::LDR, INPUT);

  screens.begin(&tft, &instrument);
  screens.setNet(&net);
  if (!loadTouchCalibration()) Serial.println(F("touch: no calibration stored -- using nominal bounds (send 'c' to calibrate)"));

  // Sensors start before the boot animation so the CO2 interrupt is already
  // collecting cycles while the splash is on screen -- the first dashboard
  // frame then has a real reading rather than dashes.
  instrument.begin();
  if (instrument.probePresent()) {
    char rom[24];
    instrument.probeSensor().romString(rom, sizeof(rom));
    Serial.printf("DS18B20: found on GPIO%d, ROM %s, %u-bit\n", pins::ONEWIRE_DATA, rom,
                  instrument.probe().resolutionBits);
  } else {
    Serial.printf("DS18B20: not found on GPIO%d (continuing without temperature)\n", pins::ONEWIRE_DATA);
  }

  // Deliberately NOT re-initialising the backlight here. On core 3.x the
  // light driver uses ledcAttach(pin), and a second ledcAttach on a pin that is
  // already bound makes the core clear the binding first -- which is how a
  // "belt and braces" re-init produced a dark screen. The rule is simpler:
  // nothing after tft.init() may touch GPIO21. backlightSelfCheck() verifies it.
  backlightSelfCheck("after diagnostics");

  // SD is entirely optional. When no card is present the logger disables
  // itself here and never touches the shared SPI3 bus again, so touch is
  // unaffected -- see the bus note at the top of Logger.h.
  if (logger.begin(&tft)) {
    Serial.printf("SD: logging to /%s (boot %lu)\n", logger.currentFile(), (unsigned long)logger.bootNumber());
  } else {
    Serial.printf("SD: %s (continuing without logging)\n", logger.lastError());
  }

  // Network last: it is optional, and nothing above waits on it.
  net.setLogger(&logger);
  net.begin(&instrument);

  bootStartMs = millis();
}

void loop() {
  static uint32_t nextUi = 0;
  static uint32_t nextSerial = 0;

  // Acquisition runs every pass and never blocks, so it is unaffected by which
  // page is displayed or by a full-screen repaint.
  instrument.update();

  const uint32_t now = millis();

  if (appState == AppState::Booting) {
    const uint32_t elapsed = now - bootStartMs;

    if (now >= nextUi) {
      nextUi = now + 40;                     // ~25 fps for the animation
      Screens::BootStatus st;
      st.co2Online  = instrument.haveReading();
      st.probePresent = instrument.probePresent();
      st.sdLogging  = logger.enabled();
      st.net        = (int)net.state();
      screens.bootFrame(elapsed, st);
    }
    serviceSerialCommands();

    // Leave the splash once there is something real to show, or give up
    // waiting so a disconnected sensor cannot hold the UI hostage.
    const bool ready = instrument.haveReading() && elapsed >= config::BOOT_MIN_MS;
    if (ready || elapsed >= config::BOOT_TIMEOUT_MS) {
      screens.bootTeardown();
      appState = AppState::Running;
      changePage(Page::Main);
      nextUi = now;
      Serial.println(ready ? F("boot complete -- first reading acquired")
                           : F("boot timeout -- entering dashboard without a reading"));
    }
    return;
  }

  serviceTouch();
  serviceSerialCommands();
  serviceBacklight();

  if (now >= nextUi) {
    nextUi = now + config::UI_REFRESH_MS;
    screens.tick();
  }

  if (now >= nextSerial) {
    nextSerial = now + config::SERIAL_REPORT_MS;
    serialReport();
  }

  logger.update(instrument);
  net.update();

  ledFault(instrument.status() == Status::SensorError ||
           instrument.status() == Status::OverRange);
}
