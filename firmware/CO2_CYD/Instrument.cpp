#include "Instrument.h"

#include "BoardPins.h"
#include "Theme.h"

void Instrument::begin() {
  // CO2 first: its interrupt should be live before anything that could take a
  // while, so no edges are missed during startup.
  _co2.begin(pins::CO2_PWM);
  _temp.begin(pins::ONEWIRE_DATA);
}

void Instrument::update() {
  _co2.update();
  _temp.update();

  const Co2Pwm::Reading& c = _co2.reading();
  const Ds18b20Sensor::Reading& p = _temp.reading();
  // Until the sensor is warm its numbers are shown live (with WARMING UP on
  // the status) but kept out of the record: no history, no session extremes,
  // no event detection. Temperature is recorded regardless.
  const bool co2Trusted = c.valid && _co2.warmedUp();
  _history.push(calibratePpm(c.averagePpm), co2Trusted, p.valid ? p.temperatureC : NAN, millis());
  if (_history.pushes() != _lastPushesSeen) {
    _lastPushesSeen = _history.pushes();
    if (co2Trusted) detectEvents();
  }

  _status = evaluateStatus();
}

// A door opening shows as a sharp fall against the reading a minute earlier.
// Both thresholds must be met so that room-air fluctuations, which are small
// in absolute terms, can never be labelled as an event.
void Instrument::detectEvents() {
  const float now  = _history.ppmFromNewest(0);
  const float then = _history.ppmFromNewest(config::EVENT_LOOKBACK_POINTS);
  if (isnan(now) || isnan(then)) return;
  const float drop = then - now;
  if (drop >= config::EVENT_DROP_PPM && drop >= then * config::EVENT_DROP_FRAC) {
    // Only flag the first point of a fall, not every point on the way down.
    bool alreadyFlagged = false;
    for (uint16_t i = 1; i <= config::EVENT_LOOKBACK_POINTS; i++) {
      if (_history.flagFromNewest(i) & History::FLAG_EVENT) { alreadyFlagged = true; break; }
    }
    if (!alreadyFlagged) _history.flagNewest(History::FLAG_EVENT);
  }
}

float Instrument::trendPpmPerMin() const {
  const float now  = _history.ppmFromNewest(0);
  const float then = _history.ppmFromNewest(config::EVENT_LOOKBACK_POINTS);
  if (isnan(now) || isnan(then)) return NAN;
  const float minutes = config::EVENT_LOOKBACK_POINTS * config::HISTORY_INTERVAL_MS / 60000.0f;
  return (now - then) / minutes;
}

float Instrument::displayPpm() const {
  const Co2Pwm::Reading& c = _co2.reading();
  return c.everValid ? calibratePpm(c.averagePpm) : NAN;
}

float Instrument::displayPercent() const {
  const float ppm = displayPpm();
  return isnan(ppm) ? NAN : Co2Pwm::ppmToPercent(ppm);
}

Status Instrument::evaluateStatus() const {
  const Co2Pwm::Reading& c = _co2.reading();

  // A signal that has gone away outranks everything else: showing a stale
  // number as if it were live is the one failure this instrument must not have.
  if (c.everValid && !c.valid) return Status::SensorError;

  // Never seen a cycle, and long enough has passed that the wire is suspect.
  if (!c.everValid) {
    return (millis() > config::CO2_STALE_MS * 2) ? Status::SensorError
                                                 : Status::WarmingUp;
  }

  // The sensor's own settling time. Its output is real but not yet trustworthy.
  if (!_co2.warmedUp()) return Status::WarmingUp;

  // At the ceiling the sensor is saying "at least this much"; a corrected
  // 7.5 % on the dashboard would read as a measurement, and it is not one.
  if (c.averagePpm >= config::CO2_OVERRANGE_PPM) return Status::OverRange;

  const float pct = Co2Pwm::ppmToPercent(calibratePpm(c.averagePpm));
  if (pct < config::CO2_LOW_PCT)  return Status::LowCo2;
  if (pct > config::CO2_HIGH_PCT) return Status::HighCo2;
  return Status::Normal;
}

const char* Instrument::statusText(Status s) {
  switch (s) {
    case Status::WarmingUp:   return "WARMING UP";
    case Status::Normal:      return "NORMAL";
    case Status::LowCo2:      return "LOW CO2";
    case Status::HighCo2:     return "HIGH CO2";
    case Status::OverRange:   return "OVER RANGE";
    case Status::SensorError: return "SENSOR ERROR";
  }
  return "?";
}

uint16_t Instrument::statusColor(Status s) {
  switch (s) {
    case Status::WarmingUp:   return theme::IDLE;
    case Status::Normal:      return theme::OK;
    case Status::LowCo2:      return theme::WARN;
    case Status::HighCo2:     return theme::WARN;
    case Status::OverRange:   return theme::ALARM;
    case Status::SensorError: return theme::ALARM;
  }
  return theme::TEXT;
}
