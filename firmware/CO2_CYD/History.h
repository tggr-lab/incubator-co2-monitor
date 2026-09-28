#pragma once
//
// Rolling history, held entirely in RAM: CO2 and temperature side by side.
//
// One point every config::HISTORY_INTERVAL_MS, in fixed rings. Nothing here
// allocates after begin(), so the measurement loop never touches the heap.
//
// A gap in either channel is stored as NaN rather than skipped, so the time
// axis stays linear and a dropout shows on the plot as a real break instead of
// being quietly stitched over.
//
#include <Arduino.h>

#include "Config.h"

class History {
 public:
  // Summary of one channel over one time window.
  struct Stats {
    float    minV = NAN;
    float    maxV = NAN;
    float    avgV = NAN;
    float    sdV  = NAN;  // sample spread, not an accuracy claim
    uint16_t count = 0;   // valid points in the window
  };

  // Offers a sample pair; stores it only when the interval has elapsed.
  // Pass co2Valid=false / tempC=NaN to record a hole for a dropout.
  void push(float ppm, bool co2Valid, float tempC, uint32_t nowMs);

  // `ago` steps back from the newest point; 0 is the latest. NaN once `ago`
  // runs past what has been collected. Indexing from the newest end is what
  // makes a partly-filled window land correctly on a time axis.
  float ppmFromNewest(uint16_t ago) const;
  float tempFromNewest(uint16_t ago) const;
  float atFromNewest(uint16_t ago) const { return ppmFromNewest(ago); }  // legacy name

  Stats ppmStats(uint16_t points) const;
  Stats tempStats(uint16_t points) const;
  Stats stats(uint16_t points) const { return ppmStats(points); }        // legacy name

  // Per-point annotation. Bit 0 marks a detected door-opening event.
  enum : uint8_t { FLAG_EVENT = 0x01 };
  void    flagNewest(uint8_t flag);
  uint8_t flagFromNewest(uint16_t ago) const;

  uint16_t size() const { return _count; }
  uint16_t capacity() const { return config::HISTORY_CAPACITY; }

  // Total points ever stored. size() saturates once the ring is full, so it
  // cannot be used to notice new data -- this can, and the UI watches it.
  uint32_t pushes() const { return _pushes; }

  static uint16_t pointsForMinutes(uint16_t minutes) {
    const uint32_t n = (uint32_t)minutes * 60000UL / config::HISTORY_INTERVAL_MS;
    return n > config::HISTORY_CAPACITY ? config::HISTORY_CAPACITY : (uint16_t)n;
  }

  // Session extremes, tracked from every sample offered rather than only the
  // decimated points that made it into the ring.
  float sessionMin() const { return _sessionMin; }
  float sessionMax() const { return _sessionMax; }
  float tempSessionMin() const { return _tempMin; }
  float tempSessionMax() const { return _tempMax; }
  void  resetSession();

 private:
  Stats statsOf(const float* buf, uint16_t points) const;
  uint16_t slotFromNewest(uint16_t ago) const {
    return (_head + config::HISTORY_CAPACITY - 1 - ago) % config::HISTORY_CAPACITY;
  }

  float    _buf[config::HISTORY_CAPACITY]     = {0};
  float    _tempBuf[config::HISTORY_CAPACITY] = {0};
  uint8_t  _flags[config::HISTORY_CAPACITY]   = {0};
  uint16_t _head  = 0;
  uint16_t _count = 0;
  uint32_t _nextMs = 0;
  uint32_t _pushes = 0;

  float _sessionMin = NAN, _sessionMax = NAN;
  float _tempMin = NAN, _tempMax = NAN;
};
