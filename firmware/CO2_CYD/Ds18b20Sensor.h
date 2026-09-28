#pragma once
//
// DS18B20 temperature probe on a single 1-Wire pin.
//
// Why this and not the BMP280: the CYD exposes exactly two free bidirectional
// pins on its connectors, and GPIO22 turned out to be unreachable through CN1.
// I2C needs both; 1-Wire needs one. GPIO27 is the one that works, and a
// waterproof stainless DS18B20 suits an incubator better than a bare breakout
// anyway. Pressure is gone with the BMP280 and is not missed here.
//
// Wiring (through a DAT/VCC/GND screw-terminal adapter):
//   red    VCC  -> 3V3
//   black  GND  -> GND
//   yellow DATA -> GPIO27      (the adapter carries the 4.7k pull-up)
//
// Absence is a normal state, not an error: with no probe the UI shows
// temperature as unavailable and everything else carries on.
//
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

class Ds18b20Sensor {
 public:
  struct Reading {
    float    temperatureC = NAN;
    bool     present      = false;   // a DS18B20 answered on the bus at begin()
    bool     valid        = false;   // the last conversion produced a sane value
    uint32_t ageMs        = 0;       // since the last valid reading
    uint32_t validCount   = 0;
    uint32_t errorCount   = 0;       // disconnected / 85 C power-on / out of range
    uint8_t  rom[8]       = {0};     // the probe's 64-bit ROM code
    uint8_t  resolutionBits = 0;
  };

  // Searches the bus. Safe with nothing wired: fails fast, never blocks.
  bool begin(int8_t pin);

  // Non-blocking. Kicks off a conversion, collects it once it has had time to
  // finish, and is otherwise a no-op. Never waits on the probe.
  void update();

  const Reading& reading() const { return _r; }

  // "28-0316A2B4C5D6" style, for the diagnostics page.
  void romString(char* out, size_t n) const;

 private:
  OneWire*           _wire = nullptr;
  DallasTemperature* _dt   = nullptr;
  Reading  _r;
  int8_t   _pin = -1;
  uint32_t _requestedAt = 0;
  uint32_t _nextRequestMs = 0;
  uint32_t _lastValidMs = 0;
  bool     _conversionPending = false;
};
