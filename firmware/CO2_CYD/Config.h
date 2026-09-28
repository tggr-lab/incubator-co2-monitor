#pragma once
//
// Every tunable the instrument has, in one place.
//
// Nothing in here is certified by anyone. The CO2 thresholds are operator
// preferences for a cell culture incubator, not a medical or manufacturer
// specification, and they are meant to be edited.
//
#include <Arduino.h>

namespace config {

// --------------------------------------------------------------- identity --
constexpr char FIRMWARE_NAME[]    = "CO2 Monitor";
constexpr char FIRMWARE_VERSION[] = "0.11.0";
constexpr char BUILD_STAMP[]      = __DATE__ " " __TIME__;
// Shown in the header of every page, above the page title.
constexpr char LAB_NAME[]         = "TGGR LAB";
constexpr char CREDIT[]           = "YAMIR";   // shown small on the splash and diagnostics

// -------------------------------------------------------------- CO2 sensor --
// Full-scale range of THIS MH-Z16, in ppm. The PWM formula multiplies by the
// part's own range, and Winsen's datasheet quotes 2000 only as an example.
// This unit is the 0-10 % VOL variant: 10 % of 1e6 = 100000 ppm. Getting this
// wrong is the classic MH-Z16 PWM bug -- a 5000 here reads 20x low.
constexpr float CO2_RANGE_PPM = 100000.0f;

// ---------------------------------------------------------- CO2 calibration --
// Software span correction, applied in exactly one place (Instrument) to the
// sensor's own number:  corrected = raw * CO2_CAL_GAIN + CO2_CAL_OFFSET_PPM.
// Everything downstream -- display, status, history, events, SD log, web --
// sees the corrected value; the raw value is kept beside it everywhere, so
// this is reversible and the original measurements are never lost.
//
// The firmware ships UNCORRECTED (gain 1.0). Do not trust the reading until
// you have set this for your own sensor: let it run in an incubator at a known
// setpoint for three days, then gain = known value / raw value. The unit this
// was developed on read about a third high at 37 C and high humidity and
// needed 0.72, while reading room air correctly. Yours will differ.
constexpr float CO2_CAL_GAIN       = 1.0f;
constexpr float CO2_CAL_OFFSET_PPM = 0.0f;
constexpr char  CO2_CAL_LABEL[]    = "x1.00 uncalibrated";   // diagnostics page

// A raw reading at or above this is the sensor's ceiling, not a measurement.
// After the gain it would look like a plausible 7.5 % when the chamber is in
// fact "at least" that, so it is reported as OVER RANGE instead.
constexpr float CO2_OVERRANGE_PPM  = 99000.0f;

// A healthy MH-Z16 PWM cycle is ~1004 ms. Anything outside this window is a
// floating pin, a glitch, or a half-captured cycle, and is discarded.
constexpr uint32_t PWM_PERIOD_MIN_US = 900000UL;
constexpr uint32_t PWM_PERIOD_MAX_US = 1100000UL;

// How long a valid reading stays trustworthy. The sensor produces one cycle a
// second, so several seconds of silence means the signal is genuinely gone
// rather than merely late.
constexpr uint32_t CO2_STALE_MS = 10000UL;

// Displayed value is a rolling mean over this many valid samples (~1 Hz each).
// Long enough to stop the last digit dancing, short enough that opening the
// incubator door still shows up as a step rather than a slow drift.
constexpr uint8_t CO2_AVERAGE_SAMPLES = 8;

// The sensor needs a few minutes before its output settles. On a cold start
// the MH-Z16 drives its PWM at 50 % duty -- exactly 50000 ppm, 5 % -- until it
// has a real measurement, which would otherwise land in the history as a huge
// spike, own the 60-minute axis, set the session max, and trip the door-event
// detector on the way down. So the warm-up gate holds until either the timer
// expires or the reading has been steady for a while *outside* that 50 % band.
constexpr uint32_t CO2_WARMUP_MS         = 180000UL;
constexpr uint32_t CO2_STEADY_MS         = 30000UL;   // steady this long -> warm
constexpr float    CO2_STEADY_FRAC       = 0.02f;     // within 2 % counts as steady
constexpr float    CO2_PREHEAT_BAND_LO   = 49000.0f;  // the 50 %-duty signature
constexpr float    CO2_PREHEAT_BAND_HI   = 51000.0f;

// --------------------------------------------------------- status limits --
// Incubator target, in percent CO2. Operator settings, not a certification.
constexpr float CO2_TARGET_PCT = 5.0f;
constexpr float CO2_LOW_PCT    = 4.5f;   // below this -> LOW CO2
constexpr float CO2_HIGH_PCT   = 5.5f;   // above this -> HIGH CO2

// ------------------------------------------------------ temperature band ---
// Operator settings for the thermometer widget: shaded band, target tick, and
// the scale the column spans. Temperature does NOT feed the instrument's
// status machine -- CO2 status is the status; temperature reports beside it.
// Nothing here is certified by anyone.
constexpr float TEMP_TARGET_C = 37.0f;
constexpr float TEMP_LOW_C    = 36.5f;
constexpr float TEMP_HIGH_C   = 37.5f;
constexpr float THERMO_MIN_C  = 15.0f;   // bottom of the column
constexpr float THERMO_MAX_C  = 45.0f;   // top of the column

// ------------------------------------------------------------ DS18B20 -----
constexpr uint8_t  DS18B20_RESOLUTION_BITS = 12;      // 0.0625 C steps
constexpr uint32_t DS18B20_CONVERSION_MS   = 800UL;   // 750 ms spec at 12 bit, plus margin
constexpr uint32_t DS18B20_INTERVAL_MS     = 2000UL;  // new reading every 2 s
constexpr uint32_t DS18B20_STALE_MS        = 15000UL; // then the value is shown as unavailable

// ------------------------------------------------------------- history ----
// One point per HISTORY_INTERVAL_MS, sized so the longest view fits in RAM.
// 60 min at 10 s per point = 360 points; the ring holds a little more so the
// 60 minute view is never short at the left edge.
constexpr uint32_t HISTORY_INTERVAL_MS = 10000UL;
constexpr uint16_t HISTORY_CAPACITY    = 384;

// ------------------------------------------------------------- timing -----
constexpr uint32_t UI_REFRESH_MS     = 500UL;
constexpr uint32_t SERIAL_REPORT_MS  = 2000UL;
constexpr uint32_t TOUCH_POLL_MS     = 30UL;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 250UL;

// Boot animation runs at least this long, and gives up waiting for the first
// valid CO2 reading after the timeout so a dead sensor cannot hang the UI.
constexpr uint32_t BOOT_MIN_MS     = 2600UL;
constexpr uint32_t BOOT_TIMEOUT_MS = 12000UL;

// ------------------------------------------------------------ events ------
// "Door opened" marker on the plot: a drop of at least this much, both in
// absolute and relative terms, between one history point and the one a minute
// earlier. The absolute floor keeps room-air noise from ever qualifying.
constexpr float    EVENT_DROP_PPM  = 2000.0f;
constexpr float    EVENT_DROP_FRAC = 0.20f;
constexpr uint16_t EVENT_LOOKBACK_POINTS = 6;   // 6 x 10 s

// Trend arrow: below this rate the reading is called steady.
constexpr float TREND_STEADY_PPM_PER_MIN = 20.0f;

// ---------------------------------------------------------- backlight -----
// Ambient auto-dim from the onboard LDR on GPIO34. The floor keeps the screen
// readable in a dark room; the ceiling is what the panel looks best at.
constexpr bool    AUTO_DIM        = true;
constexpr uint8_t BRIGHT_MAX      = 200;
constexpr uint8_t BRIGHT_MIN      = 70;
constexpr bool    LDR_DARK_IS_HIGH = true;      // raw ADC rises as the room darkens
constexpr uint16_t LDR_RAW_BRIGHT = 300;        // raw at a well-lit bench
constexpr uint16_t LDR_RAW_DARK   = 3000;       // raw with the sensor covered
constexpr uint32_t FADE_MS        = 140;        // page-change cross-fade

// ------------------------------------------------------------ network -----
// Wi-Fi is optional and off until credentials are stored in NVS with
// tools/wifi_setup.py (or the serial command `wifi <ssid> <password>`).
// Nothing in this source tree ever holds a password.
constexpr char     NET_HOSTNAME[]  = "co2";            // -> http://co2.local/
constexpr char     NTP_SERVER[]    = "pool.ntp.org";
constexpr char     TZ_INFO[]       = "UTC0";           // POSIX TZ; e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
constexpr uint32_t NET_RETRY_MS    = 15000UL;
constexpr uint16_t HTTP_PORT       = 80;

// Access-point fallback, for networks the monitor cannot join (captive
// portals, WPA2-Enterprise). With no stored credentials, or when the stored
// network has not been joined within NET_AP_FALLBACK_MS of boot, the monitor
// broadcasts its own network instead. Connect a phone or laptop to it and open
// http://192.168.4.1/ (http://co2.local/ also resolves on iOS, macOS, Linux).
// The password is a local-only default and is meant to be changed; keep it at
// 8+ characters, or make it "" for an open network. NTP cannot sync in this
// mode, so log timestamps stay blank and uptime_s is the time axis.
constexpr bool     NET_AP_ENABLED     = true;
constexpr char     NET_AP_SSID[]      = "CO2-monitor";
constexpr char     NET_AP_PASS[]      = "change-me-now";
constexpr uint32_t NET_AP_FALLBACK_MS = 60000UL;

// ------------------------------------------------------------- logging ----
constexpr uint32_t LOG_INTERVAL_MS = 10000UL;
// One file per boot: LOG_PREFIX + four-digit boot number + ".csv", e.g.
// /co2_0007.csv. The counter lives in NVS ("log"/"boot") and only advances
// when a card is present and the file opens, so a boot without a card does
// not consume a number. Columns are schema v2 (raw_ppm, cal_gain), unchanged.
constexpr char     LOG_PREFIX[]    = "/co2_";

}  // namespace config
