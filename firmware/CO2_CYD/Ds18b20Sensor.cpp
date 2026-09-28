#include "Ds18b20Sensor.h"

#include "Config.h"

bool Ds18b20Sensor::begin(int8_t pin) {
  _pin = pin;
  _wire = new OneWire(pin);
  _dt = new DallasTemperature(_wire);
  _dt->begin();
  // We poll on our own schedule; the library must never block the loop for
  // the 750 ms a 12-bit conversion takes.
  _dt->setWaitForConversion(false);

  if (_dt->getDeviceCount() == 0 || !_dt->getAddress(_r.rom, 0)) {
    _r.present = false;
    return false;
  }
  if (!_dt->validFamily(_r.rom)) {
    // Something answered on the bus but it is not a DS18x20.
    _r.present = false;
    return false;
  }

  _dt->setResolution(_r.rom, config::DS18B20_RESOLUTION_BITS);
  _r.resolutionBits = _dt->getResolution(_r.rom);
  _r.present = true;

  // First conversion straight away, so the UI does not wait a full interval.
  _dt->requestTemperaturesByAddress(_r.rom);
  _requestedAt = millis();
  _conversionPending = true;
  _nextRequestMs = millis() + config::DS18B20_INTERVAL_MS;
  return true;
}

void Ds18b20Sensor::update() {
  if (!_r.present) return;
  const uint32_t now = millis();

  if (_conversionPending) {
    // 12-bit conversions need up to 750 ms; asking earlier reads the previous
    // value or garbage. A little margin on top.
    if (now - _requestedAt < config::DS18B20_CONVERSION_MS) {
      _r.ageMs = _r.validCount ? now - _lastValidMs : now;
      return;
    }
    _conversionPending = false;

    const float t = _dt->getTempC(_r.rom);
    // -127 is the library's "disconnected" sentinel; 85.0 exactly is the
    // chip's power-on default, which means the conversion never ran; and the
    // part is specified -55..+125 C. Anything else outside plausible room /
    // incubator range is treated as a fault too, not a reading.
    const bool ok = t != DEVICE_DISCONNECTED_C && t != 85.0f &&
                    t > -40.0f && t < 85.0f;
    if (ok) {
      _r.temperatureC = t;
      _r.valid = true;
      _r.validCount++;
      _lastValidMs = now;
    } else {
      _r.errorCount++;
      // Keep the last good number for display but flag it stale once it ages
      // out; a probe unplugged mid-run must not freeze a plausible temperature
      // on screen indefinitely.
      if (_r.validCount == 0 || now - _lastValidMs > config::DS18B20_STALE_MS) _r.valid = false;
    }
  }

  if (now >= _nextRequestMs) {
    _nextRequestMs = now + config::DS18B20_INTERVAL_MS;
    _dt->requestTemperaturesByAddress(_r.rom);
    _requestedAt = now;
    _conversionPending = true;
  }

  _r.ageMs = _r.validCount ? now - _lastValidMs : now;
  if (_r.validCount && _r.ageMs > config::DS18B20_STALE_MS) _r.valid = false;
}

void Ds18b20Sensor::romString(char* out, size_t n) const {
  if (!_r.present) { snprintf(out, n, "--"); return; }
  // Family code plus the low three serial bytes. The diagnostics column is
  // 70 px wide, which holds nine characters; a longer ID ran off the screen.
  snprintf(out, n, "%02X-%02X%02X%02X", _r.rom[0], _r.rom[3], _r.rom[2], _r.rom[1]);
}
