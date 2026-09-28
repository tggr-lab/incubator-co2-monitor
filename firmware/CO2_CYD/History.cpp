#include "History.h"

#include <math.h>

void History::resetSession() {
  _sessionMin = _sessionMax = NAN;
  _tempMin = _tempMax = NAN;
}

void History::push(float ppm, bool co2Valid, float tempC, uint32_t nowMs) {
  if (co2Valid) {
    if (isnan(_sessionMin) || ppm < _sessionMin) _sessionMin = ppm;
    if (isnan(_sessionMax) || ppm > _sessionMax) _sessionMax = ppm;
  }
  if (!isnan(tempC)) {
    if (isnan(_tempMin) || tempC < _tempMin) _tempMin = tempC;
    if (isnan(_tempMax) || tempC > _tempMax) _tempMax = tempC;
  }

  if (nowMs < _nextMs) return;
  // Advance from the deadline, not from now, so spacing does not drift with
  // however late this call happened to be; resync after a long stall.
  _nextMs = (_nextMs == 0) ? nowMs + config::HISTORY_INTERVAL_MS
                           : _nextMs + config::HISTORY_INTERVAL_MS;
  if (nowMs > _nextMs) _nextMs = nowMs + config::HISTORY_INTERVAL_MS;

  _buf[_head]     = co2Valid ? ppm : NAN;
  _tempBuf[_head] = tempC;
  _flags[_head]   = 0;
  _head = (_head + 1) % config::HISTORY_CAPACITY;
  if (_count < config::HISTORY_CAPACITY) _count++;
  _pushes++;
}

float History::ppmFromNewest(uint16_t ago) const {
  return (ago >= _count) ? NAN : _buf[slotFromNewest(ago)];
}
float History::tempFromNewest(uint16_t ago) const {
  return (ago >= _count) ? NAN : _tempBuf[slotFromNewest(ago)];
}

void History::flagNewest(uint8_t flag) {
  if (_count) _flags[slotFromNewest(0)] |= flag;
}
uint8_t History::flagFromNewest(uint16_t ago) const {
  return (ago >= _count) ? 0 : _flags[slotFromNewest(ago)];
}

History::Stats History::statsOf(const float* buf, uint16_t points) const {
  Stats s;
  if (points > _count) points = _count;
  double sum = 0.0;
  for (uint16_t i = 0; i < points; i++) {
    const float v = buf[slotFromNewest(i)];
    if (isnan(v)) continue;
    if (s.count == 0 || v < s.minV) s.minV = v;
    if (s.count == 0 || v > s.maxV) s.maxV = v;
    sum += v;
    s.count++;
  }
  if (s.count == 0) return s;
  s.avgV = (float)(sum / s.count);
  // Spread of the samples themselves -- how much the reading moved. Not a
  // statement about accuracy; the manufacturer's specification is a separate
  // thing and is not modelled here.
  if (s.count > 1) {
    double acc = 0.0;
    for (uint16_t i = 0; i < points; i++) {
      const float v = buf[slotFromNewest(i)];
      if (isnan(v)) continue;
      const double d = v - s.avgV;
      acc += d * d;
    }
    s.sdV = (float)sqrt(acc / (s.count - 1));
  }
  return s;
}

History::Stats History::ppmStats(uint16_t points) const  { return statsOf(_buf, points); }
History::Stats History::tempStats(uint16_t points) const { return statsOf(_tempBuf, points); }
