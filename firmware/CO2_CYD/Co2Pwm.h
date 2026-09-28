#pragma once
//
// MH-Z16 CO2 sensor, read through its PWM output.
//
// Why PWM and not UART: this sensor was cross-validated both ways on a
// separate ESP32-S3 (UART 1506 ppm vs PWM 1499 ppm, -7 ppm apart), so the PWM
// path is known to be trustworthy on this exact unit. On the CYD it costs a
// single input pin instead of a pin pair, and GPIO35 -- which is input-only
// and therefore useless for most other jobs -- is free on header P3.
//
// The signal is a ~1 Hz pulse train. Winsen's formula, with times in ms:
//
//     ppm = RANGE * (Th - 2) / (Th + Tl - 4)
//
// RANGE is the part's own full scale, config::CO2_RANGE_PPM = 100000 for this
// 0-10 % VOL unit. The datasheet writes 2000 there only as an example; copying
// that literal is the usual reason a PWM reading comes out 20x low.
//
#include <Arduino.h>
#include "Config.h"

class Co2Pwm {
 public:
  // Everything the sensor path knows about itself. Kept whole so the
  // diagnostics screen can show the raw waveform behind a suspicious number.
  struct Reading {
    float    ppm          = 0.0f;  // most recent accepted sample
    float    averagePpm   = 0.0f;  // rolling mean, what the dashboard shows
    float    thMs         = 0.0f;  // high time of the last cycle seen
    float    tlMs         = 0.0f;  // low time of the last cycle seen
    float    periodMs     = 0.0f;  // Th + Tl, nominally ~1004 ms
    float    edgesPerSec  = 0.0f;  // 2.0 on a clean signal
    uint32_t validCount   = 0;     // cycles accepted since boot
    uint32_t rejectCount  = 0;     // cycles seen but thrown away
    uint32_t ageMs        = 0;     // since the last accepted cycle
    bool     valid        = false; // a fresh accepted sample exists
    bool     everValid    = false; // at least one accepted sample since boot
  };

  void begin(int8_t pin);

  // Drains whatever the ISR captured and folds it into the rolling state.
  // Cheap and non-blocking; call it every pass of the main loop.
  void update();

  const Reading& reading() const { return _r; }

  // Percent by volume. 50000 ppm -> 5.00 %.
  static float ppmToPercent(float ppm) { return ppm / 10000.0f; }

  // True once the sensor can be believed: the warm-up timer has run out, or
  // the reading has been steady for CO2_STEADY_MS somewhere other than the
  // 50 %-duty preheat band. Latches once true.
  bool warmedUp() const { return _warm; }
  uint32_t steadyForMs() const { return _steadySince ? millis() - _steadySince : 0; }

 private:
  // Snapshot of one complete cycle, handed from the ISR to update().
  struct Cycle {
    uint32_t thUs     = 0;
    uint32_t tlUs     = 0;
    uint32_t periodUs = 0;
  };

  bool takeCycle(Cycle& out);
  void accept(const Cycle& c);
  void reject();

  Reading  _r;
  int8_t   _pin = -1;
  uint32_t _lastValidMs = 0;

  // Rolling mean over the last N accepted samples. A plain ring rather than an
  // exponential filter, so a step change washes out in a bounded, predictable
  // number of seconds instead of trailing forever.
  float    _window[config::CO2_AVERAGE_SAMPLES] = {0};
  uint8_t  _windowCount = 0;
  uint8_t  _windowHead  = 0;

  // Edge-rate bookkeeping, for telling "wrong value" apart from "wrong signal".
  uint32_t _lastEdgeCount = 0;
  uint32_t _lastEdgeMs    = 0;

  bool     _warm = false;
  uint32_t _steadySince = 0;
  float    _steadyRef = NAN;
};
