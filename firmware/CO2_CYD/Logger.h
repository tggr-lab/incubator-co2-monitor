#pragma once
//
// Optional CSV logging to the microSD slot.
//
// Entirely optional by design: if no card is present the logger disables
// itself at boot and never touches the SPI bus again, so the instrument
// behaves exactly as if this file did not exist.
//
// ---------------------------------------------------------------------------
// The bus problem, and why it is handled the way it is
// ---------------------------------------------------------------------------
// This board asks for three SPI devices but the ESP32 has two general-purpose
// SPI hosts:
//
//   SPI2 (HSPI) : TFT panel        pins 14/13/12, CS 15
//   SPI3 (VSPI) : touch XPT2046    pins 25/32/39, CS 33
//   SPI3 (VSPI) : microSD          pins 18/23/19, CS 5     <-- same host
//
// Touch and SD are on the same peripheral but on different pins, so only one
// can be routed through it at a time, and neither library re-routes pins per
// transaction. The logger therefore borrows SPI3 for the length of a write and
// hands it straight back by re-initialising the touch controller.
//
// This is safe because everything runs in the main loop: there is no task or
// interrupt that could read the touch panel while the pins point at the card.
// Writes happen once every config::LOG_INTERVAL_MS, so the bus is on loan for
// a few milliseconds a minute.
//
#include <Arduino.h>

#include "Instrument.h"

class LgfxCyd;
class WebServer;

class Logger {
 public:
  // Probes for a card. Returns false when there is none, after which every
  // other method is a no-op and the SPI bus is left entirely alone.
  bool begin(LgfxCyd* tft);

  // Appends one record if the interval has elapsed. Non-blocking apart from
  // the write itself.
  void update(const Instrument& inst);

  bool        enabled() const { return _enabled; }
  bool        cardPresent() const { return _card; }
  const char* currentFile() const { return _file[0] ? _file + 1 : ""; }   // basename
  uint32_t    bootNumber() const { return _boot; }
  uint32_t    rowsWritten() const { return _rows; }
  const char* lastError() const { return _lastError; }

  // ---- file access for the web page. Logger owns the bus, so it does the I/O.
  struct FileInfo { char name[16]; uint32_t size; };

  // Exactly "co2_NNNN.csv": the only names the web side may touch.
  static bool validName(const char* name);

  // Root-directory log files, up to `max`. Zero when there is no card.
  size_t listFiles(FileInfo* out, size_t max);

  // Sends the file as a CSV download. The bus is borrowed for the whole
  // transfer, so touch is unresponsive until it ends. False when the name is
  // not a log file, there is no card, or the file cannot be opened.
  bool streamFile(WebServer& server, const char* name);

 private:
  bool borrowBus();
  void returnBus();
  bool openNewFile();

  LgfxCyd*    _tft = nullptr;
  bool        _enabled = false;
  bool        _card = false;
  uint32_t    _boot = 0;
  char        _file[20] = "";      // "/co2_0007.csv"
  uint32_t    _nextMs = 0;
  uint32_t    _rows = 0;
  const char* _lastError = "";
};
