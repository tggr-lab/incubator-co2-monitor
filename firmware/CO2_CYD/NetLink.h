#pragma once
//
// Optional network link: Wi-Fi station, mDNS name, NTP time, and a small
// read-only web interface.
//
// Station credentials live in NVS, put there by tools/wifi_setup.py or the
// serial command `wifi <ssid> <password>`; the source tree never contains a
// station password.
//
// Without credentials, or if the stored network cannot be joined within
// config::NET_AP_FALLBACK_MS of boot, the monitor becomes an access point
// (config::NET_AP_SSID) and serves the same pages on http://192.168.4.1/.
// A phone or laptop connects to it directly; no lab infrastructure needed.
//
// Everything is non-blocking and driven from update(). If the network never
// comes up the instrument is unaffected -- the measurement path does not know
// this module exists.
//
// Endpoints, on http://co2.local/ (config::NET_HOSTNAME) or http://192.168.4.1/:
//   /                   live page with the reading and a chart
//   /api/status         JSON snapshot
//   /api/history.json   the RAM history ring as [seconds_ago, ppm] pairs
//   /history.csv        the same ring as CSV, timestamped when NTP is synced
//
#include <Arduino.h>
#include <WebServer.h>

#include "Instrument.h"
#include "Logger.h"

class NetLink {
 public:
  enum class State : uint8_t { Off, Connecting, Online, AccessPoint };

  void begin(const Instrument* inst);
  void setLogger(Logger* log) { _log = log; }   // enables /sd/ ; call before begin()
  void update();

  State       state() const { return _state; }
  bool        serving() const { return _state == State::Online || _state == State::AccessPoint; }
  const char* summary() const { return _summary; }   // for the diagnostics page
  bool        timeSynced() const;

  // Where the clock came from. NTP (station mode) wins; the web page may set
  // it otherwise, which is what makes log timestamps possible on the hotspot.
  enum class TimeSource : uint8_t { None, Browser, Ntp };
  TimeSource  timeSource() const { return _timeSource; }
  const char* timeSourceText() const;

  // Credential management. Stored in NVS; a change takes effect immediately.
  bool setCredentials(const char* ssid, const char* pass);
  void clearCredentials();
  bool hasCredentials() const { return _ssid[0] != '\0'; }
  const char* ssid() const { return _ssid; }

 private:
  void loadCredentials();
  void startConnect();
  void startAccessPoint();
  void startServices();
  void onOnline();
  void refreshSummary();

  void handleRoot();
  void handleStatus();
  void handleHistoryJson();
  void handleHistoryCsv();
  void handleSdIndex();
  void handleSdFile();
  void handleTime();

  const Instrument* _inst = nullptr;
  Logger*           _log  = nullptr;
  TimeSource        _timeSource = TimeSource::None;
  WebServer _server{config::HTTP_PORT};
  State     _state = State::Off;
  char      _ssid[33] = "";
  char      _pass[65] = "";
  char      _summary[24] = "off";
  uint32_t  _nextRetryMs = 0;
  uint32_t  _apFallbackAtMs = 0;   // when to give up on the station and go AP
  bool      _everOnline = false;   // a link that was up gets reconnects, not AP
  bool      _serverStarted = false;
  bool      _mdnsStarted = false;
};
