#pragma once
//
// All rendering for the instrument.
//
// Three rules shape this file:
//
//   1. Nothing repaints unless its text actually changed. Every value on
//      screen goes through a TextField that remembers what it last drew.
//   2. Anything that changes shape -- the big number, the plot -- is composed
//      off-screen in one persistent sprite and pushed in a single transfer, so
//      the panel never shows a half-drawn frame.
//   3. Screens only read from Instrument. They never poll a sensor, so
//      acquisition is unaffected by which page is showing or how long a
//      repaint takes.
//
#include <Arduino.h>

#include "Instrument.h"
#include "LgfxCyd.h"

class NetLink;

enum class Page : uint8_t { Main = 0, Graph = 1, Diagnostics = 2, Connect = 3, COUNT = 4 };
enum class Span : uint8_t { Min5 = 0, Min15 = 1, Min60 = 2, COUNT = 3 };

class Screens {
 public:
  void begin(LgfxCyd* tft, const Instrument* inst);
  void setNet(const NetLink* net) { _net = net; }

  // What the splash's system-check list reports. Filled in by the sketch.
  struct BootStatus {
    bool co2Online = false;
    bool probePresent = false;
    bool sdLogging = false;
    int  net = 0;   // 0 off, 1 joining, 2 online
  };

  // ------------------------------------------------------------- boot ------
  void bootFrame(uint32_t elapsedMs, const BootStatus& st);
  void bootTeardown();
  void bootReset();

  // ------------------------------------------------------------ pages ------
  void show(Page p);
  void tick();
  Page page() const { return _page; }

  // True if an on-page control consumed the touch; false means "next page".
  bool handleTouch(int32_t x, int32_t y);

 private:
  struct TextField {
    char last[28] = {0};
    bool dirty = true;
    bool set(const char* s) {
      if (!dirty && strncmp(last, s, sizeof(last) - 1) == 0) return false;
      strncpy(last, s, sizeof(last) - 1);
      last[sizeof(last) - 1] = '\0';
      dirty = false;
      return true;
    }
    void invalidate() { dirty = true; }
  };

  // Shared off-screen buffer. 304x64 is exactly the plot width and tall enough
  // for the big number; the 128-tall plot on the graph page is rendered in two
  // passes through it rather than paying for a second buffer.
  static constexpr int SPRITE_W = 304;
  static constexpr int SPRITE_H = 64;
  bool ensureSprite();

  void drawHeader(const char* title);
  void drawLogo(int x, int y);
  void drawBootLogo(int x, int y);
  void drawLogoBitmap(const uint16_t* data, int16_t w, int16_t h, int x, int y);
  void drawPageDots();
  void drawLiveBadge();
  void drawDegreeC(int x, int y, uint16_t color);

  // Letterspaced small caps, the HUD label style used everywhere. Works on
  // the panel or on a sprite. datum: 0 left, 1 centre, 2 right (x is the anchor).
  static void drawSpaced(LovyanGFX* g, const char* text, int x, int y, int spacing, int datum);

  // Same, but any "CO2" is typeset as CO₂: the 2 in `subFont`, dropped by
  // `subDrop` px. Returns the total width; pass draw=false to only measure.
  static int drawChem(LovyanGFX* g, const char* text, int x, int y, int spacing, int datum,
                      const lgfx::IFont* mainFont, const lgfx::IFont* subFont, int subDrop,
                      bool draw = true);
  static void drawBrackets(LovyanGFX* g, int x, int y, int w, int h, int len, uint16_t color);

  void showMain();
  void tickMain();
  void drawBigNumber(const char* value, bool ok, float pct);
  void drawTrend();
  void drawThermometer(bool full);
  void drawStatsRow(int y, const char* tag, uint16_t tagColor, float now, const History::Stats& s,
                    bool isTemp, TextField& cache);

  void showGraph();
  void tickGraph();
  void drawSpanTabs();

  void showDiagnostics();
  void tickDiagnostics();

  void showConnect();
  void tickConnect();
  // A QR code on a white panel with a title and up to two caption lines.
  // `version` is a floor; the library grows it until the text fits.
  void drawQrPanel(const char* text, uint8_t version, int x, int y, int size,
                   const char* title, const char* line1, const char* line2);
  void drawCentred(const char* text, int y, const lgfx::IFont* font, uint16_t color);

  void drawPlot(int x, int y, int w, int h, uint16_t points, bool withAxes);
  static void niceBounds(float lo, float hi, float* outLo, float* outHi, float* outStep);

  LgfxCyd*          _tft  = nullptr;
  const Instrument* _inst = nullptr;
  const NetLink*    _net  = nullptr;
  Page              _page = Page::Main;
  Span              _span = Span::Min15;

  TextField _fPercent, _fPpm, _fSecondary, _fStatus, _fLive, _fTrend, _fThermo;
  TextField _fGraphCo2, _fGraphTemp;
  TextField _fDiag[18];

  uint16_t _lastStatusColor = 0;
  uint32_t _lastPlotPushes  = 0xFFFFFFFF;
  char     _connectKey[64]  = "";   // what the Connect page last drew

  LGFX_Sprite* _sprite     = nullptr;
  LGFX_Sprite* _bootSprite = nullptr;
  bool         _bootStaticDrawn = false;
};
