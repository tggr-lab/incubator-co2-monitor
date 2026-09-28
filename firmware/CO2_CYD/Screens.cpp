#include "Screens.h"

#include <math.h>
#include <string.h>

#include "Logo.h"
#include "LogoBoot.h"
#include "NetLink.h"
#include "Theme.h"

// ---------------------------------------------------------------------------
// Layout. Named so the geometry reads as a whole.
// ---------------------------------------------------------------------------
namespace {

constexpr int SCR_W = 320;
constexpr int SCR_H = 240;
constexpr int MARGIN = 8;
constexpr int HEADER_H = 28;

// Main page, top to bottom. The arch gauge and the big number share the
// sprite band; everything below is drawn direct.
constexpr int MAIN_NUM_Y     = 30;   // sprite top
constexpr int MAIN_PPM_Y     = 105;
constexpr int MAIN_TREND_Y   = 121;
constexpr int MAIN_META_Y    = 143;
constexpr int MAIN_RULE_Y    = 158;

// Lower band: dual-trace plot on the left, thermometer module on the right.
constexpr int MAIN_PLOT_X    = 8;
constexpr int MAIN_PLOT_Y    = 172;
constexpr int MAIN_PLOT_W    = 198;
constexpr int MAIN_PLOT_H    = 66;
constexpr int THERMO_X       = 214;   // module origin
constexpr int THERMO_Y       = 162;
constexpr int THERMO_W       = 98;
constexpr int THERMO_H       = 76;
constexpr int THERMO_BAR_X   = 296;   // the column itself
constexpr int THERMO_BAR_Y   = 176;
constexpr int THERMO_BAR_W   = 10;
constexpr int THERMO_BAR_H   = 58;

// Arch gauge. A large circle whose centre is well below the screen, so the
// visible part is a gentle canopy from one screen edge, over the number, to
// the other. Chosen so the arc clears the digits (|dx| <= 82 px at y >= 46)
// and, because the band is 4.5-5.5 % of a 0-10 % scale, the target lands at
// the apex: the marker climbs from the left and comes to rest at the top.
constexpr float ARCH_CX      = 160.0f;
constexpr float ARCH_CY      = 372.0f;   // screen coords
constexpr int   ARCH_R_OUT   = 340;
constexpr int   ARCH_R_IN    = 333;
constexpr float ARCH_A0      = 244.9f;   // left end, degrees (LovyanGFX: 0 = 3 o'clock, clockwise)
constexpr float ARCH_A1      = 295.1f;   // right end -- ends sit 8 px in from the sprite edges

// Graph page.
constexpr int GR_PLOT_Y  = 34;
constexpr int GR_PLOT_H  = 128;
constexpr int GR_STATS_Y = 168;
constexpr int GR_TABS_Y  = 206;
constexpr int GR_TABS_H  = 28;

constexpr int PLOT_X = MARGIN;
constexpr int PLOT_W = SCR_W - 2 * MARGIN;   // == Screens::SPRITE_W

void formatPpm(char* out, size_t n, float ppm) {
  if (isnan(ppm)) { snprintf(out, n, "--"); return; }
  const long v = lroundf(ppm);
  if (v < 1000) { snprintf(out, n, "%ld", v); return; }
  const long k = v / 1000, r = v % 1000;
  if (k < 1000) snprintf(out, n, "%ld,%03ld", k, r);
  else snprintf(out, n, "%ld,%03ld,%03ld", k / 1000, k % 1000, r);
}

// Room air reads 0.063 %, an incubator at target reads 5.02 %.
void formatPercent(char* out, size_t n, float pct) {
  if (isnan(pct)) { snprintf(out, n, "--"); return; }
  snprintf(out, n, pct >= 1.0f ? "%.2f" : "%.3f", pct);
}

uint16_t spanMinutes(Span s) {
  return s == Span::Min5 ? 5 : s == Span::Min60 ? 60 : 15;
}
const char* spanLabel(Span s) {
  return s == Span::Min5 ? "5 min" : s == Span::Min60 ? "60 min" : "15 min";
}

}  // namespace

// ---------------------------------------------------------------------------

void Screens::begin(LgfxCyd* tft, const Instrument* inst) {
  _tft = tft;
  _inst = inst;
  // Generated logo arrays are host-order RGB565. This is the switch that makes
  // pushImage(const uint16_t*) read them that way *and* honour the skip colour;
  // without it the data is taken as bus order and the transparent corners get
  // painted in bright magenta.
  _tft->setSwapBytes(true);
}

bool Screens::ensureSprite() {
  if (_sprite && _sprite->getBuffer()) return true;
  if (!_sprite) _sprite = new LGFX_Sprite(_tft);
  _sprite->setColorDepth(16);
  return _sprite->createSprite(SPRITE_W, SPRITE_H) != nullptr;
}

// ------------------------------------------------------------------ logo ---

void Screens::drawLogo(int x, int y)     { drawLogoBitmap(LOGO_DATA, LOGO_WIDTH, LOGO_HEIGHT, x, y); }
void Screens::drawBootLogo(int x, int y) { drawLogoBitmap(LOGO_BOOT_DATA, LOGO_BOOT_WIDTH, LOGO_BOOT_HEIGHT, x, y); }

void Screens::drawLogoBitmap(const uint16_t* data, int16_t w, int16_t h, int x, int y) {
  _tft->pushImage(x, y, w, h, data, (uint16_t)LOGO_TRANSPARENT);
}

// --------------------------------------------------------------- helpers ---

void Screens::drawSpaced(LovyanGFX* g, const char* text, int x, int y, int spacing, int datum) {
  int total = 0;
  for (const char* p = text; *p; p++) total += g->textWidth(String(*p).c_str()) + spacing;
  total -= spacing;
  int cx = (datum == 1) ? x - total / 2 : (datum == 2) ? x - total : x;
  g->setTextDatum(textdatum_t::top_left);
  char one[2] = {0, 0};
  for (const char* p = text; *p; p++) {
    one[0] = *p;
    g->drawString(one, cx, y);
    cx += g->textWidth(one) + spacing;
  }
}

int Screens::drawChem(LovyanGFX* g, const char* text, int x, int y, int spacing, int datum,
                      const lgfx::IFont* mainFont, const lgfx::IFont* subFont, int subDrop, bool draw) {
  // A character is the subscript when it is a '2' immediately after "CO".
  auto isSub = [&](const char* p) {
    return *p == '2' && p - text >= 2 && p[-1] == 'O' && p[-2] == 'C';
  };
  int total = 0;
  char one[2] = {0, 0};
  for (const char* p = text; *p; p++) {
    one[0] = *p;
    g->setFont(isSub(p) ? subFont : mainFont);
    total += g->textWidth(one) + spacing;
  }
  total -= spacing;
  if (!draw) { g->setFont(mainFont); return total; }

  int cx = (datum == 1) ? x - total / 2 : (datum == 2) ? x - total : x;
  g->setTextDatum(textdatum_t::top_left);
  for (const char* p = text; *p; p++) {
    one[0] = *p;
    const bool sub = isSub(p);
    g->setFont(sub ? subFont : mainFont);
    g->drawString(one, cx, y + (sub ? subDrop : 0));
    cx += g->textWidth(one) + spacing;
  }
  g->setFont(mainFont);
  return total;
}

// HUD corner brackets: four L shapes, nothing along the edges between them.
void Screens::drawBrackets(LovyanGFX* g, int x, int y, int w, int h, int len, uint16_t color) {
  const int x1 = x + w - 1, y1 = y + h - 1;
  g->drawFastHLine(x, y, len, color);        g->drawFastVLine(x, y, len, color);
  g->drawFastHLine(x1 - len + 1, y, len, color); g->drawFastVLine(x1, y, len, color);
  g->drawFastHLine(x, y1, len, color);       g->drawFastVLine(x, y1 - len + 1, len, color);
  g->drawFastHLine(x1 - len + 1, y1, len, color); g->drawFastVLine(x1, y1 - len + 1, len, color);
}

// ---------------------------------------------------------------- chrome ---

void Screens::drawDegreeC(int x, int y, uint16_t color) {
  _tft->drawCircle(x + 3, y + 4, 3, color);
  _tft->setFont(&fonts::DejaVu18);
  _tft->setTextDatum(textdatum_t::top_left);
  _tft->setTextColor(color, theme::BG);
  _tft->drawString("C", x + 9, y);
}

// Lab lockup on the left: logo mark, config::LAB_NAME in the accent colour, page title
// beneath it. The right-hand side is owned by drawLiveBadge / drawPageDots.
void Screens::drawHeader(const char* title) {
  _tft->fillRect(0, 0, SCR_W, HEADER_H + 1, theme::BG);
  drawLogo(MARGIN, 1);

  const int tx = MARGIN + LOGO_WIDTH + 8;
  _tft->setFont(&fonts::DejaVu9);
  _tft->setTextColor(theme::ACCENT, theme::BG);
  drawSpaced(_tft, config::LAB_NAME, tx, 3, 2, 0);
  _tft->setTextColor(theme::TEXT, theme::BG);
  drawChem(_tft, title, tx, 14, 1, 0, &fonts::DejaVu12, &fonts::DejaVu9, 4);

  _tft->drawFastHLine(0, HEADER_H, SCR_W, theme::LINE);
}

void Screens::drawPageDots() {
  const int n = (int)Page::COUNT, gap = 10;
  const int x0 = SCR_W - MARGIN - 4 - (n - 1) * gap;
  for (int i = 0; i < n; i++) {
    const bool on = (i == (int)_page);
    _tft->fillCircle(x0 + i * gap, HEADER_H / 2, on ? 3 : 2,
                     on ? theme::ACCENT : theme::TEXT_FAINT);
  }
}

// "LIVE" only while cycles are actually arriving; a dot alongside toggles on
// every accepted sample, so the word is demonstrably true rather than sticky.
void Screens::drawLiveBadge() {
  const bool ok = _inst->sensorOk();
  const char* txt = ok ? "LIVE" : (_inst->haveReading() ? "STALE" : "WAIT");
  const uint16_t col = ok ? theme::ACCENT : (_inst->haveReading() ? theme::WARN : theme::TEXT_FAINT);

  if (_fLive.set(txt)) {
    _tft->fillRect(SCR_W - MARGIN - 100, 4, 62, 18, theme::BG);
    _tft->setFont(&fonts::DejaVu9);
    _tft->setTextDatum(textdatum_t::middle_right);
    _tft->setTextColor(col, theme::BG);
    _tft->drawString(txt, SCR_W - MARGIN - 44, HEADER_H / 2 + 1);
  }
  static uint32_t lastCount = 0xFFFFFFFF;
  const uint32_t c = _inst->co2().validCount;
  if (c != lastCount) {
    lastCount = c;
    _tft->fillCircle(SCR_W - MARGIN - 36, HEADER_H / 2, 3, (c & 1) ? col : theme::BG);
  }

  // Wi-Fi glyph: three concentric arcs, teal when the web page is reachable,
  // faint when configured but down, absent when not configured at all.
  if (_net) {
    static int lastState = -1;
    const int st = (int)_net->state();
    if (st != lastState) {
      lastState = st;
      const int gx = SCR_W - MARGIN - 118, gy = HEADER_H / 2 + 6;
      _tft->fillRect(gx - 12, gy - 12, 24, 14, theme::BG);
      if (_net->state() != NetLink::State::Off) {
        const uint16_t col = _net->serving() ? theme::ACCENT : theme::TEXT_FAINT;
        _tft->fillCircle(gx, gy - 1, 1, col);
        _tft->drawArc(gx, gy, 4, 5, 225, 315, col);
        _tft->drawArc(gx, gy, 8, 9, 225, 315, col);
      }
    }
  }
}

// ------------------------------------------------------------------ boot ---
//
// CO2 is linear, O=C=O, and its asymmetric stretch is the vibration that makes
// the gas infrared-active -- which is what the sensor measures. Animating that
// stretch is a nod to how the instrument works, not decoration.
//
void Screens::bootFrame(uint32_t elapsedMs, const BootStatus& st) {
  constexpr int LOGO_Y = 4;
  constexpr int TITLE_Y = 118;
  constexpr int LOG_X = 22, LOG_Y = 138, LOG_DY = 11;
  constexpr int MOL_W = 108, MOL_H = 40, MOL_X = 208, MOL_Y = 144;   // clear of the log's value column
  static TextField logField[6];

  if (!_bootStaticDrawn) {
    _tft->fillScreen(theme::BG);
    drawBootLogo((SCR_W - LOGO_BOOT_WIDTH) / 2, LOGO_Y);

    _tft->setTextDatum(textdatum_t::middle_center);
    _tft->setFont(&fonts::DejaVu9);
    _tft->setTextColor(theme::TEXT_FAINT, theme::BG);
    char foot[48];
    snprintf(foot, sizeof(foot), "%s   |   %s", config::FIRMWARE_VERSION, config::CREDIT);
    _tft->drawString(foot, SCR_W / 2, 226);

    if (!_bootSprite) _bootSprite = new LGFX_Sprite(_tft);
    _bootSprite->setColorDepth(16);
    _bootSprite->createSprite(MOL_W, MOL_H);

    _bootStaticDrawn = true;
    for (auto& f : logField) f.invalidate();
  }

  // Iris reveal: each frame repaints the logo, then masks everything outside
  // the current radius with a black annulus. Repainting first is what makes
  // the mask a reveal rather than an eraser -- the annulus only ever shrinks,
  // but the pixels it uncovered on the previous frame need drawing again.
  // A 100x100 push per frame for half a second is a trivial bus load.
  {
    static int lastR = -1;
    const int cx = SCR_W / 2, cy = LOGO_Y + LOGO_BOOT_HEIGHT / 2, rMax = LOGO_BOOT_WIDTH / 2 + 2;
    int r = (int)(elapsedMs / 9);
    if (r > rMax) r = rMax;
    if (elapsedMs < 45) lastR = -1;
    if (r != lastR) {
      lastR = r;
      if (r < rMax) {
        drawBootLogo((SCR_W - LOGO_BOOT_WIDTH) / 2, LOGO_Y);
        _tft->fillArc(cx, cy, r, rMax, 0.0f, 360.0f, theme::BG);
      } else {
        drawBootLogo((SCR_W - LOGO_BOOT_WIDTH) / 2, LOGO_Y);   // final, unmasked
      }
    }
  }
  {
    static int lastStep = -1;
    int step = (int)((elapsedMs > 300 ? elapsedMs - 300 : 0) / 90);
    if (step > 5) step = 5;
    if (elapsedMs < 20) lastStep = -1;
    if (step != lastStep) {
      lastStep = step;
      const uint8_t v = (uint8_t)(255 * step / 5);
      _tft->setTextColor(_tft->color565(v, v, v), theme::BG);
      drawChem(_tft, "CO2 MONITOR", SCR_W / 2, TITLE_Y - 12, 2, 1,
               &fonts::Orbitron_Light_24, &fonts::DejaVu12, 12);
    }
  }

  // System check, one line at a time, like a machine that reports on itself.
  {
    struct Row { const char* name; const char* value; uint16_t color; };
    const Row rows[6] = {
      {"DISPLAY",    "OK",                                   theme::ACCENT},
      {"TOUCH",      "OK",                                   theme::ACCENT},
      {"CO2 SENSOR", st.co2Online ? "ONLINE" : "WAITING",     st.co2Online ? theme::ACCENT : theme::TEXT_DIM},
      {"TEMP PROBE", st.probePresent ? "OK" : "ABSENT",       st.probePresent ? theme::ACCENT : theme::TEXT_FAINT},
      {"SD CARD",    st.sdLogging ? "LOGGING" : "NONE",       st.sdLogging ? theme::ACCENT : theme::TEXT_FAINT},
      // st.net mirrors NetLink::State: 0 off, 1 connecting, 2 online, 3 access point.
      {"WI-FI",      st.net == 2 ? "ONLINE" : st.net == 3 ? "HOTSPOT" : st.net == 1 ? "JOINING" : "OFF",
                     st.net >= 2 ? theme::ACCENT : st.net == 1 ? theme::TEXT_DIM : theme::TEXT_FAINT},
    };
    const int shown = (elapsedMs < 500) ? 0 : (int)((elapsedMs - 500) / 170) + 1;
    _tft->setFont(&fonts::DejaVu9);
    for (int i = 0; i < 6 && i < shown; i++) {
      char key[40];
      snprintf(key, sizeof(key), "%s|%04X", rows[i].value, rows[i].color);
      if (!logField[i].set(key)) continue;
      const int y = LOG_Y + i * LOG_DY;
      _tft->fillRect(LOG_X, y, 180, LOG_DY, theme::BG);
      _tft->setTextColor(theme::TEXT_DIM, theme::BG);
      drawChem(_tft, rows[i].name, LOG_X, y, 1, 0, &fonts::DejaVu9, &fonts::Font0, 4);
      for (int dx = 84; dx < 118; dx += 4) _tft->drawPixel(LOG_X + dx, y + 7, theme::TEXT_FAINT);
      _tft->setTextColor(rows[i].color, theme::BG);
      drawSpaced(_tft, rows[i].value, LOG_X + 122, y, 1, 0);
    }
  }

  // O=C=O asymmetric stretch -- the vibration the sensor actually measures.
  const float swing = 5.0f * sinf(elapsedMs / 380.0f);
  if (_bootSprite && _bootSprite->getBuffer()) {
    LGFX_Sprite& s = *_bootSprite;
    s.fillSprite(theme::BG);
    const int cy = MOL_H / 2, cx = MOL_W / 2, bond = 28;
    const int oxL = (int)(cx - bond - swing), oxR = (int)(cx + bond + swing);
    const int carbon = (int)(cx + swing * 0.35f);
    for (int dy = -2; dy <= 2; dy += 4) {
      s.drawLine(oxL + 11, cy + dy, carbon - 9, cy + dy, theme::LINE);
      s.drawLine(carbon + 9, cy + dy, oxR - 11, cy + dy, theme::LINE);
    }
    s.fillCircle(oxL, cy, 10, theme::GRAPH_FILL1);  s.drawCircle(oxL, cy, 10, theme::ACCENT);
    s.fillCircle(oxR, cy, 10, theme::GRAPH_FILL1);  s.drawCircle(oxR, cy, 10, theme::ACCENT);
    s.fillCircle(carbon, cy, 8, theme::PANEL);      s.drawCircle(carbon, cy, 8, theme::TEXT_DIM);
    s.setFont(&fonts::DejaVu9);
    s.setTextDatum(textdatum_t::middle_center);
    s.setTextColor(theme::ACCENT); s.drawString("O", oxL, cy + 1); s.drawString("O", oxR, cy + 1);
    s.setTextColor(theme::TEXT);   s.drawString("C", carbon, cy + 1);
    s.pushSprite(MOL_X, MOL_Y);
  }

  const int barW = 180, barX = (SCR_W - barW) / 2;
  const float frac = fminf(1.0f, (float)elapsedMs / (float)config::BOOT_TIMEOUT_MS);
  _tft->drawRect(barX, 208, barW, 4, theme::LINE);
  _tft->fillRect(barX + 1, 209, (int)((barW - 2) * frac), 2, theme::ACCENT);
}

void Screens::bootReset() { bootTeardown(); _bootStaticDrawn = false; }

void Screens::bootTeardown() {
  if (_bootSprite) { _bootSprite->deleteSprite(); delete _bootSprite; _bootSprite = nullptr; }
}

// ----------------------------------------------------------------- pages ---

void Screens::show(Page p) {
  _page = p;
  _fPercent.invalidate(); _fPpm.invalidate(); _fSecondary.invalidate();
  _fStatus.invalidate();  _fLive.invalidate(); _fTrend.invalidate();
  _fGraphCo2.invalidate(); _fGraphTemp.invalidate(); _fThermo.invalidate();
  for (auto& f : _fDiag) f.invalidate();
  _lastStatusColor = 0xFFFF;
  _lastPlotPushes  = 0xFFFFFFFF;

  ensureSprite();
  switch (p) {
    case Page::Main:        showMain(); break;
    case Page::Graph:       showGraph(); break;
    case Page::Diagnostics: showDiagnostics(); break;
    case Page::Connect:     showConnect(); break;
    default: break;
  }
  tick();
}

void Screens::tick() {
  switch (_page) {
    case Page::Main:        tickMain(); break;
    case Page::Graph:       tickGraph(); break;
    case Page::Diagnostics: tickDiagnostics(); break;
    case Page::Connect:     tickConnect(); break;
    default: break;
  }
}

// ------------------------------------------------------------------ MAIN ---

void Screens::showMain() {
  _tft->fillScreen(theme::BG);
  drawHeader("CO2 MONITOR");
  drawPageDots();

  _tft->drawFastHLine(0, MAIN_RULE_Y, SCR_W, theme::LINE);
  _tft->setFont(&fonts::DejaVu9);
  _tft->setTextColor(theme::TEXT_FAINT, theme::BG);
  drawSpaced(_tft, "LAST 15 MIN", MAIN_PLOT_X, MAIN_RULE_Y + 3, 1, 0);
  // Legend for the two traces.
  _tft->fillRect(MAIN_PLOT_X + 84, MAIN_RULE_Y + 6, 8, 2, theme::ACCENT);
  drawChem(_tft, "CO2", MAIN_PLOT_X + 95, MAIN_RULE_Y + 3, 1, 0, &fonts::DejaVu9, &fonts::Font0, 4);
  _tft->fillRect(MAIN_PLOT_X + 122, MAIN_RULE_Y + 6, 8, 2, theme::WARN);
  drawSpaced(_tft, "TEMP", MAIN_PLOT_X + 133, MAIN_RULE_Y + 3, 1, 0);

  drawThermometer(true);
}

// Temperature module: a slim column with the incubator band shaded and the
// target ticked, a "mercury" fill to the current reading, the value in clean
// digits, and the session range. It lives in its own bracketed frame so it
// reads as a second instrument rather than a caption on the first.
void Screens::drawThermometer(bool full) {
  const Ds18b20Sensor::Reading& p = _inst->probe();
  const float t = p.valid ? p.temperatureC : NAN;

  auto toY = [&](float c) {
    float f = (c - config::THERMO_MIN_C) / (config::THERMO_MAX_C - config::THERMO_MIN_C);
    if (f < 0) f = 0; if (f > 1) f = 1;
    return THERMO_BAR_Y + THERMO_BAR_H - 1 - (int)(f * (THERMO_BAR_H - 1));
  };

  if (full) {
    _tft->fillRect(THERMO_X, THERMO_Y, THERMO_W, THERMO_H, theme::BG);
    drawBrackets(_tft, THERMO_X, THERMO_Y, THERMO_W, THERMO_H, 8, theme::ACCENT_DEEP);
    _tft->setFont(&fonts::DejaVu9);
    _tft->setTextColor(theme::TEXT_FAINT, theme::BG);
    drawSpaced(_tft, "TEMPERATURE", THERMO_X + 6, THERMO_Y + 5, 1, 0);
  }

  // Cache key: value to 0.1 C, validity, session range to 0.1 C.
  char key[40];
  snprintf(key, sizeof(key), "%.1f|%d|%.1f|%.1f", isnan(t) ? -99.0f : t, p.present,
           _inst->history().tempSessionMin(), _inst->history().tempSessionMax());
  if (!full && !_fThermo.set(key)) return;
  if (full) _fThermo.set(key);

  // ---- column -----------------------------------------------------------------
  const int bx = THERMO_BAR_X, by = THERMO_BAR_Y, bw = THERMO_BAR_W, bh = THERMO_BAR_H;
  _tft->fillRect(bx - 1, by - 1, bw + 2, bh + 2, theme::BG);
  _tft->fillRect(bx, by, bw, bh, theme::TRACK);
  const int yLo = toY(config::TEMP_LOW_C), yHi = toY(config::TEMP_HIGH_C);
  _tft->fillRect(bx, yHi, bw, yLo - yHi + 1, theme::TRACK_BAND);
  _tft->drawFastHLine(bx - 2, toY(config::TEMP_TARGET_C), bw + 4, theme::ACCENT);
  {
    // The one number the column needs: the target, beside its tick.
    char b[8];
    snprintf(b, sizeof(b), "%.0f", config::TEMP_TARGET_C);
    _tft->setFont(&fonts::DejaVu9);
    _tft->setTextColor(theme::ACCENT, theme::BG);
    // Sits just under its tick: the readout's unit occupies the row beside it.
    _tft->setTextDatum(textdatum_t::top_right);
    const int ty = toY(config::TEMP_TARGET_C);
    _tft->fillRect(bx - 18, ty + 2, 15, 11, theme::BG);   // only the label's own box
    _tft->drawString(b, bx - 3, ty + 3);
  }

  const bool inBand = !isnan(t) && t >= config::TEMP_LOW_C && t <= config::TEMP_HIGH_C;
  const uint16_t col = isnan(t) ? theme::TEXT_FAINT : inBand ? theme::ACCENT : theme::WARN;
  if (!isnan(t)) {
    const int yt = toY(t);
    _tft->fillRect(bx + 3, yt, bw - 6, by + bh - yt, col);       // mercury
    _tft->fillRect(bx - 2, yt - 1, bw + 4, 3, theme::TEXT);       // meniscus marker
  }

  // ---- readout ------------------------------------------------------------------
  const int tx = THERMO_X + 6;
  _tft->fillRect(tx, THERMO_Y + 16, THERMO_BAR_X - 14 - tx, 56, theme::BG);
  _tft->setTextDatum(textdatum_t::top_left);
  if (!isnan(t)) {
    char v[12];
    snprintf(v, sizeof(v), "%.1f", t);
    _tft->setFont(&fonts::DejaVu18);
    _tft->setTextColor(theme::TEXT, theme::BG);
    _tft->drawString(v, tx, THERMO_Y + 20);
    const int vx = tx + _tft->textWidth(v) + 4;
    _tft->drawCircle(vx + 3, THERMO_Y + 23, 2, theme::TEXT_DIM);
    _tft->setFont(&fonts::DejaVu12);
    _tft->setTextColor(theme::TEXT_DIM, theme::BG);
    _tft->drawString("C", vx + 8, THERMO_Y + 21);
  } else {
    _tft->setFont(&fonts::DejaVu18);
    _tft->setTextColor(theme::TEXT_FAINT, theme::BG);
    _tft->drawString(p.present ? "--.-" : "n/a", tx, THERMO_Y + 20);
  }

  _tft->setFont(&fonts::DejaVu9);
  _tft->setTextColor(col, theme::BG);
  drawSpaced(_tft, isnan(t) ? (p.present ? "NO DATA" : "NO PROBE")
                            : inBand ? "IN BAND" : (t < config::TEMP_LOW_C ? "LOW" : "HIGH"),
             tx, THERMO_Y + 46, 1, 0);

  char lo[8], hi[8], rng[20];
  const float tmin = _inst->history().tempSessionMin(), tmax = _inst->history().tempSessionMax();
  if (isnan(tmin)) snprintf(rng, sizeof(rng), "-- / --");
  else { snprintf(lo, sizeof(lo), "%.1f", tmin); snprintf(hi, sizeof(hi), "%.1f", tmax);
         snprintf(rng, sizeof(rng), "%s / %s", lo, hi); }
  _tft->setTextColor(theme::TEXT_DIM, theme::BG);
  _tft->setTextDatum(textdatum_t::top_left);
  _tft->drawString(rng, tx, THERMO_Y + 60);
}

// Big number under the arch gauge, composed off-screen and pushed as one block.
// The arch lives inside this sprite because the sprite band is exactly where
// its visible portion falls; drawing it direct would be erased on every push.
void Screens::drawBigNumber(const char* value, bool ok, float pct) {
  if (!ensureSprite()) return;
  LGFX_Sprite& s = *_sprite;
  s.fillSprite(theme::BG);

  // ---- arch gauge -----------------------------------------------------------
  const int cx = (int)ARCH_CX - MARGIN;
  const int cy = (int)ARCH_CY - MAIN_NUM_Y;
  const float fullPct = config::CO2_RANGE_PPM / 10000.0f;
  auto angleFor = [&](float p) {
    float f = p / fullPct;
    if (f < 0) f = 0; if (f > 1) f = 1;
    return ARCH_A0 + f * (ARCH_A1 - ARCH_A0);
  };

  s.fillArc(cx, cy, ARCH_R_IN, ARCH_R_OUT, ARCH_A0, ARCH_A1, theme::TRACK);
  s.fillArc(cx, cy, ARCH_R_IN - 2, ARCH_R_OUT + 2,
            angleFor(config::CO2_LOW_PCT), angleFor(config::CO2_HIGH_PCT), theme::TRACK_BAND);

  const bool haveValue = ok && !isnan(pct);
  if (haveValue) {
    const bool inBand = pct >= config::CO2_LOW_PCT && pct <= config::CO2_HIGH_PCT;
    const uint16_t col = inBand ? theme::ACCENT : theme::WARN;
    // Filled progress from the left end to the reading, then a marker dot.
    s.fillArc(cx, cy, ARCH_R_IN, ARCH_R_OUT, ARCH_A0, angleFor(pct), col);
    const float a = angleFor(pct) * (float)M_PI / 180.0f;
    const float rm = (ARCH_R_IN + ARCH_R_OUT) * 0.5f;
    const int mx = cx + (int)lroundf(cosf(a) * rm);
    const int my = cy + (int)lroundf(sinf(a) * rm);
    s.fillCircle(mx, my, 5, theme::BG);
    s.fillCircle(mx, my, 4, col);
    s.fillCircle(mx, my, 2, theme::TEXT);
  }
  // Scale ends.
  s.setFont(&fonts::DejaVu9);
  s.setTextColor(theme::TEXT_FAINT);
  s.setTextDatum(textdatum_t::top_left);   s.drawString("0", 6, 42);
  s.setTextDatum(textdatum_t::top_right);  s.drawString("10 %", SPRITE_W - 4, 42);

  // Tick marks along the outside of the arch, one per percent; longer at the
  // ends and at 5 %.
  for (int k = 0; k <= 10; k++) {
    const float a = angleFor((float)k) * (float)M_PI / 180.0f;
    const bool major = (k % 5) == 0;
    const int r0 = ARCH_R_OUT + 2, r1 = ARCH_R_OUT + (major ? 8 : 5);
    s.drawLine(cx + (int)lroundf(cosf(a) * r0), cy + (int)lroundf(sinf(a) * r0),
               cx + (int)lroundf(cosf(a) * r1), cy + (int)lroundf(sinf(a) * r1),
               k == 5 ? theme::ACCENT : theme::TEXT_FAINT);
  }

  // HUD frame around the readout band.
  drawBrackets(&s, 0, 0, SPRITE_W, SPRITE_H, 10, theme::ACCENT_DEEP);

  // ---- the number: seven-segment, with unlit segments ghosted ---------------
  // Font7 is monospaced, so a ghost string with the same character pattern
  // ("8.888" for "0.164") lands exactly under the live digits.
  char ghost[16];
  for (size_t k = 0; k < sizeof(ghost) - 1 && value[k]; k++) {
    ghost[k] = (value[k] >= '0' && value[k] <= '9') ? '8' : value[k];
    ghost[k + 1] = '\0';
  }
  constexpr int UNIT_GAP = 10;
  s.setFont(&fonts::Font7);
  const int numW = s.textWidth(ghost);
  s.setFont(&fonts::DejaVu24);
  const int unitW = s.textWidth("%");
  const int x0 = (SPRITE_W - (numW + UNIT_GAP + unitW)) / 2;
  const int ny = 40;   // 48 px glyphs centred here fill rows 16..63, under the arch's sag

  s.setFont(&fonts::Font7);
  s.setTextDatum(textdatum_t::middle_left);
  s.setTextColor(theme::GHOST);
  s.drawString(ghost, x0, ny);
  s.setTextColor(ok ? theme::READOUT : theme::TEXT_DIM);
  s.drawString(value, x0, ny);

  s.setFont(&fonts::DejaVu24);
  s.setTextColor(theme::TEXT_DIM);
  s.drawString("%", x0 + numW + UNIT_GAP, ny + 10);

  s.pushSprite(MARGIN, MAIN_NUM_Y);
}

// Small trend line under the ppm: direction glyph plus rate.
void Screens::drawTrend() {
  const float t = _inst->trendPpmPerMin();
  char buf[28];
  if (isnan(t))                                        snprintf(buf, sizeof(buf), "TREND  --");
  else if (fabsf(t) < config::TREND_STEADY_PPM_PER_MIN) snprintf(buf, sizeof(buf), "STEADY");
  else                                                 snprintf(buf, sizeof(buf), "%s %.0f PPM/MIN", t > 0 ? "RISING" : "FALLING", fabsf(t));
  if (!_fTrend.set(buf)) return;

  _tft->fillRect(60, MAIN_TREND_Y - 7, 200, 14, theme::BG);
  _tft->setFont(&fonts::DejaVu9);
  const int tw = _tft->textWidth(buf) + (int)strlen(buf);   // +1 px letterspacing
  const bool arrow = !isnan(t) && fabsf(t) >= config::TREND_STEADY_PPM_PER_MIN;
  const int total = tw + (arrow ? 12 : 0);
  int x = (SCR_W - total) / 2;
  if (arrow) {
    const int y = MAIN_TREND_Y;
    const uint16_t col = t > 0 ? theme::ACCENT : theme::WARN;
    if (t > 0) _tft->fillTriangle(x, y + 4, x + 8, y + 4, x + 4, y - 4, col);
    else       _tft->fillTriangle(x, y - 4, x + 8, y - 4, x + 4, y + 4, col);
    x += 12;
  }
  _tft->setTextColor(theme::TEXT_DIM, theme::BG);
  drawSpaced(_tft, buf, x, MAIN_TREND_Y - 5, 1, 0);
}

void Screens::tickMain() {
  char buf[28];

  // ---- big percentage -----------------------------------------------------
  const float pct = _inst->displayPercent();
  formatPercent(buf, sizeof(buf), pct);
  // Repaint when the digits change or the marker would move a pixel.
  {
    char key[40];
    const int mpos = isnan(pct) ? -1 : (int)(pct * 40.0f);
    snprintf(key, sizeof(key), "%s|%d|%d", buf, mpos, _inst->sensorOk());
    if (_fPercent.set(key)) drawBigNumber(buf, _inst->sensorOk(), pct);
  }

  // ---- ppm ----------------------------------------------------------------
  // With a span correction in force the sensor's own number sits beside it,
  // so the display never hides what the hardware actually said.
  char ppmTxt[20], rawTxt[20];
  formatPpm(ppmTxt, sizeof(ppmTxt), _inst->displayPpm());
  if (config::CO2_CAL_GAIN != 1.0f || config::CO2_CAL_OFFSET_PPM != 0.0f) {
    formatPpm(rawTxt, sizeof(rawTxt), _inst->rawAveragePpm());
    snprintf(buf, sizeof(buf), "%s ppm (raw %s)", ppmTxt, rawTxt);
  } else {
    snprintf(buf, sizeof(buf), "%s ppm", ppmTxt);
  }
  if (_fPpm.set(buf)) {
    _tft->fillRect(0, MAIN_PPM_Y - 11, SCR_W, 22, theme::BG);
    _tft->setFont(&fonts::DejaVu18);
    _tft->setTextDatum(textdatum_t::middle_center);
    _tft->setTextColor(theme::TEXT_DIM, theme::BG);
    _tft->drawString(buf, SCR_W / 2, MAIN_PPM_Y);
  }

  drawTrend();

  // ---- secondary slot: CO2 session range (temperature has its own module) --
  {
    char lo[12], hi[12], secondary[28];
    formatPpm(lo, sizeof(lo), _inst->history().sessionMin());
    formatPpm(hi, sizeof(hi), _inst->history().sessionMax());
    snprintf(secondary, sizeof(secondary), "%s / %s", lo, hi);
    if (_fSecondary.set(secondary)) {
      _tft->fillRect(0, MAIN_META_Y - 13, 150, 27, theme::BG);
      _tft->setTextDatum(textdatum_t::top_left);
      _tft->setFont(&fonts::DejaVu9);
      _tft->setTextColor(theme::TEXT_FAINT, theme::BG);
      drawSpaced(_tft, "SESSION MIN / MAX", MARGIN + 2, MAIN_META_Y - 12, 1, 0);
      _tft->setFont(&fonts::DejaVu12);
      _tft->setTextColor(theme::TEXT_DIM, theme::BG);
      _tft->drawString(secondary, MARGIN + 2, MAIN_META_Y);
    }
  }

  // ---- status pill --------------------------------------------------------
  const Status st = _inst->status();
  const char* stText = Instrument::statusText(st);
  const uint16_t stCol = Instrument::statusColor(st);
  if (_fStatus.set(stText) || stCol != _lastStatusColor) {
    _lastStatusColor = stCol;
    _tft->fillRect(160, MAIN_META_Y - 13, SCR_W - 160, 27, theme::BG);
    const int tw = drawChem(_tft, stText, 0, 0, 0, 0, &fonts::DejaVu12, &fonts::DejaVu9, 4, false);
    const int pw = tw + 24, ph = 22;
    const int px = SCR_W - MARGIN - pw, py = MAIN_META_Y - 10;
    _tft->fillRoundRect(px, py, pw, ph, ph / 2, stCol);
    _tft->setTextColor(theme::BG, stCol);
    drawChem(_tft, stText, px + pw / 2, py + 4, 0, 1, &fonts::DejaVu12, &fonts::DejaVu9, 4);
  }

  drawLiveBadge();
  drawThermometer(false);

  // ---- rolling plot: only when a new point has landed ---------------------
  if (_inst->history().pushes() != _lastPlotPushes) {
    _lastPlotPushes = _inst->history().pushes();
    drawPlot(MAIN_PLOT_X, MAIN_PLOT_Y, MAIN_PLOT_W, MAIN_PLOT_H, History::pointsForMinutes(15), false);
  }
}

// ----------------------------------------------------------------- GRAPH ---

void Screens::showGraph() {
  _tft->fillScreen(theme::BG);
  drawHeader("CO2 HISTORY");
  drawPageDots();
  drawSpanTabs();
}

void Screens::drawSpanTabs() {
  const int w = (SCR_W - 2 * MARGIN) / (int)Span::COUNT;
  for (int i = 0; i < (int)Span::COUNT; i++) {
    const bool on = (i == (int)_span);
    const int x = MARGIN + i * w;
    _tft->fillRoundRect(x + 2, GR_TABS_Y, w - 4, GR_TABS_H, 6, on ? theme::ACCENT_DEEP : theme::PANEL);
    if (on) _tft->drawRoundRect(x + 2, GR_TABS_Y, w - 4, GR_TABS_H, 6, theme::ACCENT);
    _tft->setFont(&fonts::DejaVu12);
    _tft->setTextDatum(textdatum_t::middle_center);
    _tft->setTextColor(on ? theme::TEXT : theme::TEXT_DIM, on ? theme::ACCENT_DEEP : theme::PANEL);
    _tft->drawString(spanLabel((Span)i), x + w / 2, GR_TABS_Y + GR_TABS_H / 2 + 1);
  }
}

void Screens::tickGraph() {
  const History& h = _inst->history();
  const uint16_t pts = History::pointsForMinutes(spanMinutes(_span));

  if (h.pushes() != _lastPlotPushes) {
    _lastPlotPushes = h.pushes();
    drawPlot(PLOT_X, GR_PLOT_Y, PLOT_W, GR_PLOT_H, pts, true);
  }

  drawStatsRow(GR_STATS_Y,      "CO2",  theme::ACCENT, _inst->displayPpm(), h.ppmStats(pts),  false, _fGraphCo2);
  drawStatsRow(GR_STATS_Y + 17, "TEMP", theme::WARN,
               _inst->probe().valid ? _inst->probe().temperatureC : NAN, h.tempStats(pts), true, _fGraphTemp);
  drawLiveBadge();
}

// One channel's NOW / MIN / MAX / MEAN on a single line, with a colour swatch
// tying it to its trace.
void Screens::drawStatsRow(int y, const char* tag, uint16_t tagColor, float now,
                           const History::Stats& s, bool isTemp, TextField& cache) {
  char vals[4][12];
  auto fmt = [&](char* out, float v) {
    if (isnan(v)) snprintf(out, 12, "--");
    else if (isTemp) snprintf(out, 12, "%.1f", v);
    else formatPpm(out, 12, v);
  };
  fmt(vals[0], now); fmt(vals[1], s.minV); fmt(vals[2], s.maxV); fmt(vals[3], s.avgV);
  char key[56];
  snprintf(key, sizeof(key), "%s|%s|%s|%s", vals[0], vals[1], vals[2], vals[3]);
  if (!cache.set(key)) return;

  _tft->fillRect(MARGIN, y, SCR_W - 2 * MARGIN, 15, theme::BG);
  _tft->fillRect(MARGIN, y + 5, 8, 2, tagColor);
  _tft->setFont(&fonts::DejaVu9);
  _tft->setTextColor(tagColor, theme::BG);
  drawChem(_tft, tag, MARGIN + 12, y + 1, 1, 0, &fonts::DejaVu9, &fonts::Font0, 4);

  // Labels measured, not assumed, so four stats always fit the width.
  static const char* labels[4] = {"NOW", "MIN", "MAX", "AVG"};
  int x = MARGIN + 42;
  for (int i = 0; i < 4; i++) {
    _tft->setTextColor(theme::TEXT_FAINT, theme::BG);
    drawSpaced(_tft, labels[i], x, y + 1, 1, 0);
    x += _tft->textWidth(labels[i]) + (int)strlen(labels[i]) + 3;
    _tft->setTextDatum(textdatum_t::top_left);
    _tft->setTextColor(i == 0 ? theme::TEXT : theme::TEXT_DIM, theme::BG);
    _tft->drawString(vals[i], x, y + 1);
    x += _tft->textWidth("0,000") + 6;
  }
}

// ----------------------------------------------------------- DIAGNOSTICS ---

void Screens::showDiagnostics() {
  _tft->fillScreen(theme::BG);
  drawHeader("DIAGNOSTICS");
  drawPageDots();

  static const char* labels[18] = {
      "CO2 ppm", "CO2 %", "raw ppm", "Th", "Tl", "PWM period", "edges/s", "valid/rej", "last valid",
      "DS18B20", "probe temp", "probe age", "probe ID", "network", "ambient raw", "uptime", "free heap", "trend"};
  _tft->setFont(&fonts::DejaVu9);
  _tft->setTextDatum(textdatum_t::top_left);
  _tft->setTextColor(theme::TEXT_DIM, theme::BG);
  for (int i = 0; i < 18; i++) {
    drawChem(_tft, labels[i], MARGIN + 2 + (i / 9) * 158, 36 + (i % 9) * 18, 0, 0,
             &fonts::DejaVu9, &fonts::Font0, 4);
  }
  _tft->drawFastHLine(0, 198, SCR_W, theme::LINE);
  _tft->setTextColor(theme::TEXT_FAINT, theme::BG);
  char build[72];
  snprintf(build, sizeof(build), "%s %s", config::FIRMWARE_VERSION, config::BUILD_STAMP);
  _tft->drawString(build, MARGIN + 2, 206);
  _tft->setTextDatum(textdatum_t::top_right);
  _tft->drawString(config::CREDIT, SCR_W - MARGIN - 2, 206);
  _tft->setTextDatum(textdatum_t::top_left);
  // Kept under 45 characters: the line is drawn in a spaced 9 px face and a
  // longer one ran off the right edge of the panel.
  char hw[72];
  snprintf(hw, sizeof(hw), "PWM 35 | 1W 27 | cal %s", config::CO2_CAL_LABEL);
  _tft->drawString(hw, MARGIN + 2, 220);
}

void Screens::tickDiagnostics() {
  const Co2Pwm::Reading& c = _inst->co2();
  const Ds18b20Sensor::Reading& b = _inst->probe();
  char v[24];
  auto row = [&](int i, const char* text) {
    if (!_fDiag[i].set(text)) return;
    const int x = MARGIN + 2 + (i / 9) * 158 + 82;
    const int y = 36 + (i % 9) * 18;
    _tft->fillRect(x, y, 72, 13, theme::BG);
    _tft->setFont(&fonts::DejaVu9);
    _tft->setTextDatum(textdatum_t::top_left);
    _tft->setTextColor(theme::TEXT, theme::BG);
    _tft->drawString(text, x, y);
  };
  formatPpm(v, sizeof(v), _inst->displayPpm());              row(0, v);
  formatPercent(v, sizeof(v), _inst->displayPercent());      row(1, v);
  formatPpm(v, sizeof(v), _inst->rawPpm());                  row(2, v);
  snprintf(v, sizeof(v), "%.1f ms", c.thMs);                 row(3, v);
  snprintf(v, sizeof(v), "%.1f ms", c.tlMs);                 row(4, v);
  snprintf(v, sizeof(v), "%.1f ms", c.periodMs);             row(5, v);
  snprintf(v, sizeof(v), "%.1f", c.edgesPerSec);             row(6, v);
  snprintf(v, sizeof(v), "%lu/%lu", (unsigned long)c.validCount, (unsigned long)c.rejectCount); row(7, v);
  snprintf(v, sizeof(v), "%.1f s", c.ageMs / 1000.0f);       row(8, v);

  // DS18B20 block: presence, value, freshness, identity.
  if (!b.present)   snprintf(v, sizeof(v), "absent");
  else if (b.valid) snprintf(v, sizeof(v), "ok  %u-bit", b.resolutionBits);
  else              snprintf(v, sizeof(v), "no data");
  row(9, v);
  if (b.valid) snprintf(v, sizeof(v), "%.2f C", b.temperatureC); else snprintf(v, sizeof(v), "--");
  row(10, v);
  if (b.present && b.validCount) snprintf(v, sizeof(v), "%.1f s  e%lu", b.ageMs / 1000.0f, (unsigned long)b.errorCount);
  else snprintf(v, sizeof(v), "--");
  row(11, v);
  _inst->probeSensor().romString(v, sizeof(v));               row(12, v);

  if (_net) snprintf(v, sizeof(v), "%s", _net->summary()); else snprintf(v, sizeof(v), "off");
  row(13, v);
  snprintf(v, sizeof(v), "%u", (unsigned)_inst->ambientRaw()); row(14, v);
  const uint32_t up = _inst->uptimeSec();
  snprintf(v, sizeof(v), "%luh %02lum %02lus", (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60), (unsigned long)(up % 60)); row(15, v);
  snprintf(v, sizeof(v), "%u", (unsigned)ESP.getFreeHeap());  row(16, v);
  const float t = _inst->trendPpmPerMin();
  if (isnan(t)) snprintf(v, sizeof(v), "--"); else snprintf(v, sizeof(v), "%+.0f /min", t);
  row(17, v);
  drawLiveBadge();
}

// ------------------------------------------------------------------ PLOT ---

void Screens::niceBounds(float lo, float hi, float* outLo, float* outHi, float* outStep) {
  if (isnan(lo) || isnan(hi)) { *outLo = 0; *outHi = 1000; *outStep = 500; return; }
  float range = hi - lo;
  const float minRange = fmaxf(100.0f, hi * 0.02f);
  if (range < minRange) {
    const float mid = (hi + lo) * 0.5f;
    lo = mid - minRange * 0.5f; hi = mid + minRange * 0.5f; range = minRange;
  }
  const float raw = range / 3.0f;
  const float mag = powf(10.0f, floorf(log10f(raw)));
  const float norm = raw / mag;
  float step = (norm <= 1.0f) ? 1.0f : (norm <= 2.0f) ? 2.0f : (norm <= 5.0f) ? 5.0f : 10.0f;
  step *= mag;
  *outLo = floorf(lo / step) * step;
  *outHi = ceilf(hi / step) * step;
  if (*outLo < 0) *outLo = 0;
  *outStep = step;
}

// Renders the plot through the shared sprite in horizontal passes, so a plot
// taller than the sprite costs no extra memory. Time runs left to right with
// "now" pinned to the right edge; a window that is not yet full leaves blank
// space on the left, which is the truthful picture.
void Screens::drawPlot(int x, int y, int w, int h, uint16_t points, bool withAxes) {
  if (!ensureSprite() || w > SPRITE_W) return;
  const History& hist = _inst->history();
  const History::Stats s = hist.ppmStats(points);
  const History::Stats ts = hist.tempStats(points);

  float lo, hi, step;
  niceBounds(s.minV, s.maxV, &lo, &hi, &step);
  const float span = (hi > lo) ? (hi - lo) : 1.0f;
  const bool haveData = s.count >= 2;

  // Temperature axis: at least a 1 C window, edges on 0.5 C, so a steady
  // probe draws a steady line rather than full-scale noise.
  const bool haveTemp = ts.count >= 2;
  float tlo = 0, thi = 1;
  if (haveTemp) {
    tlo = ts.minV; thi = ts.maxV;
    if (thi - tlo < 1.0f) { const float m = (thi + tlo) * 0.5f; tlo = m - 0.5f; thi = m + 0.5f; }
    tlo = floorf(tlo * 2.0f) / 2.0f; thi = ceilf(thi * 2.0f) / 2.0f;
  }
  const float tspan = (thi > tlo) ? (thi - tlo) : 1.0f;
  auto toYt = [&](float c) {
    float f = (c - tlo) / tspan;
    if (f < 0) f = 0; if (f > 1) f = 1;
    return (int)((h - 1) - f * (h - 1));
  };

  // Geometry in plot-local coordinates (0..h).
  auto toY = [&](float ppm) {
    float f = (ppm - lo) / span;
    if (f < 0) f = 0; if (f > 1) f = 1;
    return (int)((h - 1) - f * (h - 1));
  };
  const float bandLo = config::CO2_LOW_PCT * 10000.0f, bandHi = config::CO2_HIGH_PCT * 10000.0f;
  const bool showBand = haveData && bandHi > lo && bandLo < hi;

  LGFX_Sprite& sp = *_sprite;
  int lastLabelX = -1000;   // x of the last event label drawn, across passes
  for (int passTop = 0; passTop < h; passTop += SPRITE_H) {
    const int passH = (h - passTop < SPRITE_H) ? (h - passTop) : SPRITE_H;
    sp.fillSprite(theme::BG);
    const int oy = -passTop;   // plot-local -> sprite-local

    // CRT scanline texture: every other row, faint enough to read as tone.
    for (int ry = (passTop & 1) ? 1 : 0; ry < passH; ry += 2) sp.drawFastHLine(0, ry, w, theme::SCAN);

    if (!haveData) {
      if (passTop == 0) {
        sp.setFont(&fonts::DejaVu9);
        sp.setTextDatum(textdatum_t::middle_center);
        sp.setTextColor(theme::TEXT_FAINT);
        sp.drawString("collecting data...", w / 2, (h < SPRITE_H ? h : SPRITE_H) / 2);
      }
    } else {
      if (showBand) {
        const int yTop = toY(fminf(bandHi, hi)), yBot = toY(fmaxf(bandLo, lo));
        sp.fillRect(0, yTop + oy, w, (yBot - yTop) + 1, theme::GRAPH_BAND);
      }
      for (float g = lo; g <= hi + 0.001f; g += step) {
        const int gy = toY(g);
        if (gy > 0 && gy < h - 1) sp.drawFastHLine(0, gy + oy, w, theme::GRAPH_GRID);
      }

      int prevY = -1, lastX = -1, lastY = -1;
      for (int px = 0; px < w; px++) {
        const uint16_t ago = (uint16_t)((uint32_t)(w - 1 - px) * (points - 1) / (w - 1));
        const float v = hist.atFromNewest(ago);
        if (isnan(v)) { prevY = -1; continue; }
        const int cy = toY(v);

        // Gradient fill: three bands between the trace and the baseline.
        const int total = (h - 1) - cy;
        const int b1 = total / 3, b2 = total * 2 / 3;
        sp.drawFastVLine(px, cy + oy,          b1,         theme::GRAPH_FILL1);
        sp.drawFastVLine(px, cy + b1 + oy,     b2 - b1,    theme::GRAPH_FILL2);
        sp.drawFastVLine(px, cy + b2 + oy,     total - b2, theme::GRAPH_FILL3);

        if (prevY >= 0) {
          sp.drawLine(px - 1, prevY + oy, px, cy + oy, theme::GRAPH_LINE);
          sp.drawLine(px - 1, prevY + 1 + oy, px, cy + 1 + oy, theme::GRAPH_LINE);
        } else {
          sp.drawFastVLine(px, cy + oy, 2, theme::GRAPH_LINE);
        }
        prevY = cy; lastX = px; lastY = cy;

        // Door-opening marker. A dotted guide from the top down to the trace
        // -- never through the fill, which read as a tear in the data -- a
        // flag at the top, and a label only where one fits without colliding
        // with the previous event's label.
        if (hist.flagFromNewest(ago) & History::FLAG_EVENT) {
          const uint16_t agoPrev = (px == 0) ? 0xFFFF :
              (uint16_t)((uint32_t)(w - px) * (points - 1) / (w - 1));
          if (agoPrev != ago) {           // draw once per point, not per column
            for (int yy = 9; yy < cy - 1; yy += 3) sp.drawPixel(px, yy + oy, theme::TEXT_FAINT);
            sp.fillTriangle(px - 4, 1 + oy, px + 4, 1 + oy, px, 7 + oy, theme::WARN);
            // Label only in the clear middle: the axis labels own both top corners.
            if (withAxes && px - lastLabelX >= 40 && px >= 52 && px + 34 < w - 48) {
              sp.setFont(&fonts::DejaVu9);
              sp.setTextDatum(textdatum_t::top_left);
              sp.setTextColor(theme::WARN);
              sp.drawString("DOOR", px + 6, 1 + oy);
              lastLabelX = px;
            }
          }
        }
      }
      if (lastX >= 0) {
        sp.fillCircle(lastX, lastY + oy, 3, theme::BG);
        sp.fillCircle(lastX, lastY + oy, 2, theme::TEXT);
      }

      // Temperature trace on top: thin, orange, its own axis. A door opening
      // shows here as both traces dipping together.
      if (haveTemp) {
        int prevT = -1;
        for (int px = 0; px < w; px++) {
          const uint16_t ago = (uint16_t)((uint32_t)(w - 1 - px) * (points - 1) / (w - 1));
          const float tv = hist.tempFromNewest(ago);
          if (isnan(tv)) { prevT = -1; continue; }
          const int cy = toYt(tv);
          if (prevT >= 0) sp.drawLine(px - 1, prevT + oy, px, cy + oy, theme::WARN);
          else            sp.drawPixel(px, cy + oy, theme::WARN);
          prevT = cy;
        }
      }
    }

    sp.drawFastHLine(0, (h - 1) + oy, w, theme::LINE);   // baseline
    drawBrackets(&sp, 0, 0 + oy, w, h, 8, theme::ACCENT_DEEP);

    if (withAxes && haveData) {
      // Drawn last, with an opaque background, so nothing underneath -- trace,
      // fill, or an event flag near the edge -- can collide with them.
      char b[16];
      sp.setFont(&fonts::DejaVu9);
      sp.setTextColor(theme::TEXT_FAINT, theme::BG);
      sp.setTextDatum(textdatum_t::top_left);
      formatPpm(b, sizeof(b), hi);  sp.drawString(b, 12, 3 + oy);
      sp.setTextDatum(textdatum_t::bottom_left);
      formatPpm(b, sizeof(b), lo);  sp.drawString(b, 12, (h - 5) + oy);
      sp.setTextDatum(textdatum_t::top_right);
      sp.drawString(spanLabel(_span), w - 12, 3 + oy);
      if (haveTemp) {
        // Right-hand temperature axis, in the trace's own colour.
        sp.setTextColor(theme::WARN, theme::BG);
        snprintf(b, sizeof(b), "%.1f", thi);
        sp.setTextDatum(textdatum_t::top_right);    sp.drawString(b, w - 12, 14 + oy);
        snprintf(b, sizeof(b), "%.1f", tlo);
        sp.setTextDatum(textdatum_t::bottom_right); sp.drawString(b, w - 12, (h - 5) + oy);
      }
    }

    // Push only the region this pass owns. The sprite is a fixed 304x64, so a
    // narrower or shorter plot is clipped at the panel rather than resized.
    _tft->setClipRect(x, y + passTop, w, passH);
    sp.pushSprite(x, y + passTop);
    _tft->clearClipRect();
  }
}

// ----------------------------------------------------------------- TOUCH ---

bool Screens::handleTouch(int32_t x, int32_t y) {
  if (_page == Page::Graph && y >= GR_TABS_Y - 14) {
    const int w = (SCR_W - 2 * MARGIN) / (int)Span::COUNT;
    int idx = (x - MARGIN) / w;
    if (idx < 0) idx = 0;
    if (idx >= (int)Span::COUNT) idx = (int)Span::COUNT - 1;
    if ((Span)idx != _span) {
      _span = (Span)idx;
      drawSpanTabs();
      _lastPlotPushes = 0xFFFFFFFF;
      _fGraphCo2.invalidate(); _fGraphTemp.invalidate();
    }
    return true;
  }
  return false;
}

// --------------------------------------------------------------- CONNECT ---
// QR codes for the phone: in hotspot mode, one to join the network and one
// to open the page; on a joined network, one for the page's address.
// LovyanGFX draws the codes itself. The panels are white with black modules
// regardless of theme, because phone cameras expect that contrast.

namespace {
constexpr int QR_Y    = 38;     // panel top
constexpr int QR_SIZE = 128;    // panel side; the code sits inset by QR_PAD
constexpr int QR_PAD  = 7;
}  // namespace

void Screens::drawCentred(const char* text, int y, const lgfx::IFont* font, uint16_t color) {
  _tft->setFont(font);
  _tft->setTextDatum(textdatum_t::top_center);
  _tft->setTextColor(color, theme::BG);
  _tft->drawString(text, SCR_W / 2, y);
}

void Screens::drawQrPanel(const char* text, uint8_t version, int x, int y, int size,
                          const char* title, const char* line1, const char* line2) {
  _tft->fillRoundRect(x, y, size, size, 6, TFT_WHITE);
  _tft->qrcode(text, x + QR_PAD, y + QR_PAD, size - 2 * QR_PAD, version);
  const int cx = x + size / 2;
  _tft->setFont(&fonts::DejaVu9);
  _tft->setTextColor(theme::ACCENT, theme::BG);
  drawSpaced(_tft, title, cx, y + size + 7, 1, 1);
  _tft->setFont(&fonts::DejaVu12);
  _tft->setTextDatum(textdatum_t::top_center);
  _tft->setTextColor(theme::TEXT, theme::BG);
  if (line1 && *line1) _tft->drawString(line1, cx, y + size + 21);
  _tft->setTextColor(theme::TEXT_DIM, theme::BG);
  if (line2 && *line2) _tft->drawString(line2, cx, y + size + 37);
}

void Screens::showConnect() {
  _tft->fillScreen(theme::BG);
  drawHeader("CONNECT");
  drawPageDots();
  _connectKey[0] = '\0';   // force tickConnect to paint
}

void Screens::tickConnect() {
  const int st = _net ? (int)_net->state() : 0;
  char key[64];
  snprintf(key, sizeof(key), "%d|%s", st, _net ? _net->summary() : "");
  if (strcmp(key, _connectKey) == 0) return;
  strncpy(_connectKey, key, sizeof(_connectKey) - 1);
  _connectKey[sizeof(_connectKey) - 1] = '\0';

  _tft->fillRect(0, HEADER_H + 1, SCR_W, SCR_H - HEADER_H - 1, theme::BG);

  if (!_net || st == (int)NetLink::State::Off) {
    drawCentred("WI-FI OFF", 110, &fonts::DejaVu18, theme::TEXT_DIM);
    return;
  }
  if (st == (int)NetLink::State::Connecting) {
    char buf[48];
    snprintf(buf, sizeof(buf), "JOINING %s", _net->ssid());
    drawCentred(buf, 100, &fonts::DejaVu18, theme::TEXT_DIM);
    drawCentred("hotspot starts if this fails", 130, &fonts::DejaVu12, theme::TEXT_FAINT);
    return;
  }
  if (st == (int)NetLink::State::AccessPoint) {
    // Two steps, left to right. Version 1 is a floor: the library grows it
    // until the text fits, so each code gets the largest modules it can.
    char wifi[96];
    snprintf(wifi, sizeof(wifi), "WIFI:T:%s;S:%s;P:%s;;",
             config::NET_AP_PASS[0] ? "WPA" : "nopass", config::NET_AP_SSID, config::NET_AP_PASS);
    drawQrPanel(wifi, 1, 14, QR_Y, QR_SIZE, "1  JOIN WI-FI", config::NET_AP_SSID,
                config::NET_AP_PASS[0] ? config::NET_AP_PASS : "open network");
    drawQrPanel("http://192.168.4.1/", 1, SCR_W - 14 - QR_SIZE, QR_Y, QR_SIZE, "2  OPEN PAGE",
                "192.168.4.1", "");
    return;
  }
  // Online on a real network: one code for the address.
  char url[48];
  snprintf(url, sizeof(url), "http://%s/", _net->summary());
  drawQrPanel(url, 1, (SCR_W - QR_SIZE) / 2, QR_Y, QR_SIZE, "OPEN PAGE", "co2.local", _net->summary());
  char ssid[48];
  snprintf(ssid, sizeof(ssid), "on %s", _net->ssid());
  drawCentred(ssid, QR_Y + QR_SIZE + 54, &fonts::DejaVu9, theme::TEXT_FAINT);
}
