#include "Co2Pwm.h"

// ---------------------------------------------------------------------------
// ISR-side state.
//
// The interrupt records complete cycles into a one-slot mailbox guarded by a
// seqlock: the counter is bumped before and after the write, so a reader that
// sees the same even count on both sides knows it read a consistent snapshot.
// That avoids disabling interrupts in the main loop, and 32-bit aligned loads
// and stores are atomic on the Xtensa core, so no other locking is needed.
// ---------------------------------------------------------------------------
namespace {

volatile uint32_t g_seq = 0;      // even = stable, odd = write in progress
volatile uint32_t g_thUs = 0;
volatile uint32_t g_tlUs = 0;
volatile uint32_t g_periodUs = 0;

volatile uint32_t g_edges = 0;
volatile uint32_t g_prevRiseUs = 0;
volatile uint32_t g_lastFallUs = 0;
volatile bool     g_haveCycle = false;

int8_t g_pin = -1;

// Two edges closer together than this are ringing, not signal. The tightest
// real pulse this sensor can produce is Th = 2 ms at 0 ppm, so 200 us leaves
// an order of magnitude of headroom while still swallowing contact noise.
constexpr uint32_t GLITCH_US = 200;

void IRAM_ATTR pwmIsr() {
  const uint32_t now = micros();
  const bool high = digitalRead(g_pin);

  // Reject implausibly narrow pulses before they can corrupt a cycle.
  const uint32_t sinceEdge = now - (high ? g_lastFallUs : g_prevRiseUs);
  if (g_edges && sinceEdge < GLITCH_US) return;

  // Explicit read-modify-write rather than ++: incrementing a volatile is
  // deprecated in C++20, and spelling it out makes the ordering the seqlock
  // depends on visible.
  g_edges = g_edges + 1;

  if (high) {
    // A rising edge closes the previous cycle: Th was measured on the last
    // falling edge, and the low time runs from that fall to now.
    if (g_prevRiseUs && g_lastFallUs) {
      const uint32_t period = now - g_prevRiseUs;
      const uint32_t tl     = now - g_lastFallUs;

      g_seq = g_seq + 1;       // -> odd, readers back off
      g_periodUs = period;
      g_tlUs     = tl;
      g_seq = g_seq + 1;       // -> even, snapshot complete
      g_haveCycle = true;
    }
    g_prevRiseUs = now;
  } else if (g_prevRiseUs) {
    // Falling edge: the high time is now known. Stored outside the seqlock
    // window on purpose -- it is republished with the cycle on the next rise.
    g_thUs = now - g_prevRiseUs;
    g_lastFallUs = now;
  }
}

}  // namespace

void Co2Pwm::begin(int8_t pin) {
  _pin = pin;
  g_pin = pin;

  // GPIO35 is input-only: it has no output driver at all, so pinMode(OUTPUT)
  // on it would silently do nothing. INPUT is the only correct mode here, and
  // the sensor drives the line actively, so no pull resistor is wanted.
  pinMode(pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(pin), pwmIsr, CHANGE);

  _lastEdgeMs = millis();
}

bool Co2Pwm::takeCycle(Cycle& out) {
  if (!g_haveCycle) return false;

  // Seqlock read, retried a bounded number of times. A cycle only lands once a
  // second, so contention here is effectively impossible; the loop exists to
  // be correct rather than because it is expected to spin.
  for (int attempt = 0; attempt < 4; attempt++) {
    const uint32_t before = g_seq;
    if (before & 1u) continue;            // ISR mid-write
    out.periodUs = g_periodUs;
    out.tlUs     = g_tlUs;
    out.thUs     = g_thUs;
    if (g_seq == before) {
      g_haveCycle = false;
      return true;
    }
  }
  return false;
}

void Co2Pwm::reject() {
  _r.rejectCount++;
}

void Co2Pwm::accept(const Cycle& c) {
  const float th     = c.thUs / 1000.0f;
  const float period = c.periodUs / 1000.0f;

  // Winsen's formula. The -2 / -4 ms terms are the sensor's own fixed framing
  // overhead, not a calibration fudge.
  float ppm = config::CO2_RANGE_PPM * (th - 2.0f) / (period - 4.0f);
  if (ppm < 0.0f) ppm = 0.0f;
  if (ppm > config::CO2_RANGE_PPM) ppm = config::CO2_RANGE_PPM;

  _r.ppm      = ppm;
  _r.thMs     = th;
  _r.tlMs     = c.tlUs / 1000.0f;
  _r.periodMs = period;

  _window[_windowHead] = ppm;
  _windowHead = (_windowHead + 1) % config::CO2_AVERAGE_SAMPLES;
  if (_windowCount < config::CO2_AVERAGE_SAMPLES) _windowCount++;

  float sum = 0.0f;
  for (uint8_t i = 0; i < _windowCount; i++) sum += _window[i];
  _r.averagePpm = sum / _windowCount;

  _r.validCount++;
  _r.everValid  = true;
  _lastValidMs  = millis();

  // Steadiness: how long the rolling mean has stayed within a few percent of
  // a reference. The reference re-anchors whenever the mean wanders off it.
  if (isnan(_steadyRef) || fabsf(_r.averagePpm - _steadyRef) > _steadyRef * config::CO2_STEADY_FRAC + 20.0f) {
    _steadyRef = _r.averagePpm;
    _steadySince = _lastValidMs;
  }
  if (!_warm) {
    const bool inPreheatBand = _r.averagePpm >= config::CO2_PREHEAT_BAND_LO &&
                               _r.averagePpm <= config::CO2_PREHEAT_BAND_HI;
    const bool steadyLongEnough = _windowCount >= config::CO2_AVERAGE_SAMPLES &&
                                  (_lastValidMs - _steadySince) >= config::CO2_STEADY_MS;
    if (_lastValidMs >= config::CO2_WARMUP_MS || (steadyLongEnough && !inPreheatBand)) {
      _warm = true;
      Serial.printf("CO2: warm-up complete at %lus (%s), %.0f ppm\n",
                    (unsigned long)(_lastValidMs / 1000), 
                    _lastValidMs >= config::CO2_WARMUP_MS ? "timer" : "steady reading", _r.averagePpm);
    }
  }
}

void Co2Pwm::update() {
  const uint32_t now = millis();

  Cycle c;
  if (takeCycle(c)) {
    // A cycle has to survive all of these to be believed. Each test names a
    // distinct failure: a wrong period means the wrong signal entirely, a
    // zero or over-long Th means a half-captured cycle, and an inconsistent
    // Th + Tl means an edge was missed somewhere in the middle.
    const bool periodOk = c.periodUs >= config::PWM_PERIOD_MIN_US &&
                          c.periodUs <= config::PWM_PERIOD_MAX_US;
    const bool thOk     = c.thUs >= 1000UL && c.thUs < c.periodUs;
    const int32_t drift = (int32_t)(c.thUs + c.tlUs) - (int32_t)c.periodUs;
    const bool sumOk    = drift > -5000 && drift < 5000;

    if (periodOk && thOk && sumOk) {
      accept(c);
    } else {
      // Keep the offending waveform visible for diagnostics, but never let it
      // reach ppm -- a single malformed cycle must not jump the display.
      _r.thMs     = c.thUs / 1000.0f;
      _r.tlMs     = c.tlUs / 1000.0f;
      _r.periodMs = c.periodUs / 1000.0f;
      reject();
    }
  }

  // Edge rate, sampled about once a second. A clean 1 Hz square wave gives
  // exactly 2.0; near zero means the wire is off, and a large number means the
  // interrupt is catching noise rather than the sensor.
  if (now - _lastEdgeMs >= 1000UL) {
    const uint32_t edges = g_edges;
    _r.edgesPerSec = (edges - _lastEdgeCount) * 1000.0f / (now - _lastEdgeMs);
    _lastEdgeCount = edges;
    _lastEdgeMs    = now;
  }

  _r.ageMs = _r.everValid ? (now - _lastValidMs) : now;
  _r.valid = _r.everValid && _r.ageMs <= config::CO2_STALE_MS;
}
