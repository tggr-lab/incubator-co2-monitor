#pragma once
//
// Visual language for the instrument, in one place.
//
// The accent and warning colours are the TGGR Lab brand colours, sampled from
// the logo itself (teal #007078, orange #E08010) and lightened only as much as
// this TN panel needs to render them legibly. Using the lab's own palette for
// the data is what makes the logo feel like it belongs on the screen rather
// than being stuck onto it.
//
// Semantics are deliberately narrow: teal means live/good, orange means out of
// band, red means the instrument cannot measure, grey means not yet. Nothing
// else on the screen is allowed to use those colours.
//
#include <Arduino.h>

namespace theme {

// RGB565, named by role.
constexpr uint16_t BG         = 0x0000;  // true black -- this panel needs every bit of contrast
constexpr uint16_t PANEL      = 0x1082;  // card surface, just clear of the ground
constexpr uint16_t LINE       = 0x2124;  // hairlines, separators
constexpr uint16_t TEXT       = 0xFFFF;  // primary readout
constexpr uint16_t TEXT_DIM   = 0xA534;  // labels, units, secondary values
constexpr uint16_t TEXT_FAINT = 0x630C;  // axis ticks, hints

// Brand teal: exact for fills, lifted for lines and text on the dark ground.
constexpr uint16_t ACCENT      = 0x1D15;  // #1FA3AA  lines, live data, selection
constexpr uint16_t ACCENT_DEEP = 0x038F;  // #007078  fills, the logo's own teal

constexpr uint16_t OK    = ACCENT;        // NORMAL
constexpr uint16_t WARN  = 0xE402;        // #E08010  LOW / HIGH CO2 -- the logo's orange
constexpr uint16_t ALARM = 0xE249;        // #E5484D  SENSOR ERROR
constexpr uint16_t IDLE  = 0x8410;        // WARMING UP

// Seven-segment readout: lit segments in phosphor white, unlit ones as ghosts.
// Tuned on the real panel, not the framebuffer: its TN cell lifts blacks to
// blue, which pushes any dark teal ghost up into competition with the lit
// digits. Pure white for lit segments and a near-floor ghost is what actually
// reads as "seven-segment with unlit segments" through the glass.
constexpr uint16_t READOUT = 0xFFFF;  // lit segments
constexpr uint16_t GHOST   = 0x0862;  // #081010 unlit segment, a whisper above the floor
constexpr uint16_t SCAN    = 0x0841;  // #080808 CRT scanline texture

// Plot. Three fill bands under the trace fake a vertical gradient cheaply.
constexpr uint16_t GRAPH_LINE  = ACCENT;
constexpr uint16_t GRAPH_FILL1 = 0x0A48;  // nearest the line
constexpr uint16_t GRAPH_FILL2 = 0x0186;
constexpr uint16_t GRAPH_FILL3 = 0x00C3;  // nearest the baseline
constexpr uint16_t GRAPH_GRID  = 0x18E3;
constexpr uint16_t GRAPH_BAND  = 0x0145;  // shaded target band

// Target track under the reading.
constexpr uint16_t TRACK      = 0x2965;
constexpr uint16_t TRACK_BAND = ACCENT_DEEP;

}  // namespace theme
