#pragma once
//
// The instrument's data model: the two sensors, the history ring, and the one
// status value derived from them.
//
// The UI reads from here and never talks to a sensor directly, so acquisition
// keeps running at full rate no matter which screen is on display.
//
#include <Arduino.h>

#include "Ds18b20Sensor.h"
#include "Co2Pwm.h"
#include "Config.h"
#include "History.h"

enum class Status : uint8_t {
  WarmingUp,
  Normal,
  LowCo2,
  HighCo2,
  OverRange,     // raw reading at the sensor's ceiling; the number is a floor, not a value
  SensorError,
};

class Instrument {
 public:
  void begin();
  void update();

  // --------------------------------------------------------------- state --
  Status status() const { return _status; }
  static const char* statusText(Status s);
  static uint16_t    statusColor(Status s);

  // Value the dashboard shows: the rolling mean with the software span
  // correction applied, so the last digit does not dance. Returns NaN until
  // the first valid cycle has been accepted.
  float displayPpm() const;
  float displayPercent() const;

  // The one place config::CO2_CAL_* is applied. Everything the instrument
  // reports goes through here; Co2Pwm itself stays uncorrected.
  static float calibratePpm(float rawPpm) {
    return isnan(rawPpm) ? NAN : rawPpm * config::CO2_CAL_GAIN + config::CO2_CAL_OFFSET_PPM;
  }

  // Rolling mean before correction: what the sensor itself says. Logged beside
  // the corrected value so the correction can be revised without losing data.
  float rawAveragePpm() const { return _co2.reading().everValid ? _co2.reading().averagePpm : NAN; }

  // Latest single accepted cycle, unsmoothed and uncorrected. Diagnostics only.
  float rawPpm() const { return _co2.reading().everValid ? _co2.reading().ppm : NAN; }

  bool haveReading() const { return _co2.reading().everValid; }
  bool sensorOk() const { return _co2.reading().valid; }

  const Co2Pwm::Reading&       co2() const { return _co2.reading(); }
  const Ds18b20Sensor::Reading& probe() const { return _temp.reading(); }
  const Ds18b20Sensor&          probeSensor() const { return _temp; }
  const History&               history() const { return _history; }

  bool probePresent() const { return _temp.reading().present; }

  uint32_t uptimeSec() const { return millis() / 1000UL; }

  // Rate of change over the last minute of history, ppm/min. NaN until there
  // is a minute of data.
  float trendPpmPerMin() const;

  // Onboard light sensor, raw ADC and the brightness derived from it.
  uint16_t ambientRaw() const { return _ambientRaw; }
  void     setAmbientRaw(uint16_t v) { _ambientRaw = v; }

 private:
  Status evaluateStatus() const;

  Co2Pwm       _co2;
  Ds18b20Sensor _temp;
  History      _history;
  Status       _status = Status::WarmingUp;
  uint32_t     _lastPushesSeen = 0;
  uint16_t     _ambientRaw = 0;

  void detectEvents();
};
