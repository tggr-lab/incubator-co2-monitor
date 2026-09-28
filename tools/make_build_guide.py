#!/usr/bin/env python3
"""
Generate the build guide from one source of truth.

    tools/make_build_guide.py

Writes:
    docs/wiring-map.svg     standalone wiring map (literal colours, dark ground)
    docs/index.html   the illustrated guide (theme-aware page fragment)
    docs/BUILD_GUIDE.md     the same guide for reading on GitHub

The wiring map and the flowcharts are defined once here, so the page, the
image and the Markdown cannot drift apart. Pin numbers and thresholds mirror
BoardPins.h and Config.h; change them there first, then here.
"""
import html, os

import re, base64
HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(HERE, "..", "docs")

def config():
    """Values straight from Config.h, so the guide cannot quote a stale number."""
    txt = open(os.path.join(HERE, "..", "firmware", "CO2_CYD", "Config.h")).read()
    out = {}
    for name, val in re.findall(r"constexpr\s+[\w ]+?\s+(\w+)(?:\[\])?\s*=\s*([^;]+);", txt):
        out[name] = val.strip().strip('"').rstrip("fFuUlL")
    return out
CFG = config()
GAIN = CFG["CO2_CAL_GAIN"]
VERSION = CFG["FIRMWARE_VERSION"]
OVER = f'{int(float(CFG["CO2_OVERRANGE_PPM"])):,}'
AP_SSID = CFG["NET_AP_SSID"]

# --------------------------------------------------------------- wiring map --
CSS_PALETTE = dict(ink="var(--ink)", dim="var(--ink-dim)", faint="var(--ink-faint)",
                   panel="var(--panel)", block="var(--block)", line="var(--line)",
                   accent="var(--accent)", w5="var(--w5)", w3="var(--w3)",
                   wg="var(--wg)", wpwm="var(--wpwm)", wdat="var(--wdat)", ground="none")
DARK_PALETTE = dict(ink="#E6EEEE", dim="#A9B8B9", faint="#6F8183", panel="#101A1B",
                    block="#18282A", line="#2A3D3F", accent="#2BB5BC", w5="#F0625A",
                    w3="#F09A2E", wg="#93A3A5", wpwm="#2BB5BC", wdat="#E3C341",
                    ground="#0A1112")

def wiring_svg(p, standalone):
    o = []
    a = o.append
    font = ' font-family="IBM Plex Sans, system-ui, sans-serif"' if standalone else ""
    xmlns = ' xmlns="http://www.w3.org/2000/svg"' if standalone else ""
    a(f'<svg{xmlns} viewBox="0 0 980 620" role="img"{font} font-size="12" '
      f'aria-label="Wiring map: the MH-Z16 and the DS18B20 probe in the sensor head connect '
      f'through a junction box to connectors P1, P3 and CN1 on the ESP32-2432S028R board">')
    if standalone:
        a(f'<rect x="0" y="0" width="980" height="620" style="fill:{p["ground"]}"/>')

    def box(x, y, w, h, fill, dash=False):
        d = ' stroke-dasharray="5 4"' if dash else ""
        a(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" '
          f'style="fill:{fill};stroke:{p["line"]};stroke-width:1.5"{d}/>')

    def text(x, y, s, col="ink", anchor="start", weight=None, size=None, spacing=None):
        extra = f' text-anchor="{anchor}"' if anchor != "start" else ""
        if weight:  extra += f' font-weight="{weight}"'
        if size:    extra += f' font-size="{size}"'
        if spacing: extra += f' letter-spacing="{spacing}"'
        a(f'<text x="{x}" y="{y}"{extra} style="fill:{p[col]}">{s}</text>')

    def pad(x, y, col):
        a(f'<rect x="{x-4}" y="{y-4}" width="8" height="8" style="fill:{p[col]}"/>')

    def wire(points, col, flow=True):
        pts = " ".join(f"{x},{y}" for x, y in points)
        a(f'<polyline points="{pts}" style="fill:none;stroke:{p[col]};stroke-width:2.5;'
          f'stroke-linejoin:round;stroke-linecap:round"/>')
        # Page version only: moving dots. Signals travel sensor to board, which is
        # the direction the points are listed in; power travels the other way.
        direction = {"wpwm": "wiredot", "wdat": "wiredot", "w5": "wiredot rev", "w3": "wiredot rev"}.get(col)
        if flow and not standalone and direction:
            a(f'<polyline class="{direction}" points="{pts}"/>')

    # ---- containers -----------------------------------------------------------
    box(20, 50, 230, 520, p["panel"], dash=True)
    text(35, 78, "SENSOR HEAD", "accent", weight=600, spacing="1.5")
    text(35, 95, "sits inside the incubator", "dim")

    box(330, 50, 270, 520, p["panel"], dash=True)
    text(345, 78, "JUNCTION BOX", "accent", weight=600, spacing="1.5")
    text(345, 95, "on the bench, outside", "dim")

    box(700, 50, 260, 520, p["panel"])
    text(716, 78, "ESP32-2432S028R", "accent", weight=600, spacing="1.5")

    # ---- sensor head ------------------------------------------------------------
    box(45, 120, 180, 150, p["block"])
    text(57, 142, "MH-Z16", weight=600, size=14)
    text(57, 258, "CO₂, 0–10 % vol", "dim")
    box(45, 340, 180, 150, p["block"])
    text(57, 362, "DS18B20", weight=600, size=14)
    text(57, 478, "waterproof probe", "dim")

    upper = [(160, "+5 V", "w5"), (195, "GND", "wg"), (230, "PWM", "wpwm")]
    lower = [(380, "GND", "wg"), (415, "DATA", "wdat"), (450, "VCC", "w3")]
    for y, label, col in upper + lower:
        text(213, y + 4, label, anchor="end")

    # ---- junction box -----------------------------------------------------------
    text(380, 138, "lever connectors × 3", "dim")
    text(380, 338, "DS18B20 adapter", "dim")
    text(380, 500, "pull-up resistor on the adapter", "dim")
    box(380, 350, 140, 130, p["block"])

    # ---- wires: sensor head to junction box ---------------------------------------
    for y, _, col in upper:
        wire([(225, y), (380, y)], col)
        wire([(450, y), (600, y)], col)
    for y, _, col in lower:
        wire([(225, y), (380, y)], col)
        wire([(520, y), (600, y)], col)
    for y, _, col in upper:
        a(f'<rect x="380" y="{y-12}" width="70" height="24" rx="3" '
          f'style="fill:{p["block"]};stroke:{p[col]};stroke-width:1.5"/>')
    for y, label, col in lower:
        text(392, y + 4, {"DATA": "DAT"}.get(label, label))
        pad(380, y, col); pad(520, y, col)
    for y, _, col in upper + lower:
        pad(225, y, col)

    text(290, 252, "sensor lead", "faint", anchor="middle")
    text(290, 472, "probe lead", "faint", anchor="middle")
    for y, s in ((160, "+5 V"), (195, "GND"), (230, "PWM, 1 cycle/s")):
        text(525, y - 7, s, "dim", anchor="middle")
    for y, s in ((380, "GND"), (415, "data"), (450, "3V3")):
        text(560, y - 7, s, "dim", anchor="middle")

    # ---- wires: junction box to board (lanes chosen so nothing crosses) -----------
    wire([(600, 160), (620, 160), (620, 150), (700, 150)], "w5")
    wire([(600, 195), (660, 195), (660, 240), (700, 240)], "wg")
    wire([(600, 230), (640, 230), (640, 330), (700, 330)], "wpwm")
    wire([(600, 380), (660, 380), (660, 450), (700, 450)], "wg")
    wire([(600, 415), (640, 415), (640, 510), (700, 510)], "wdat")
    wire([(600, 450), (620, 450), (620, 540), (700, 540)], "w3")

    # ---- board connectors -----------------------------------------------------------
    conns = [
        ("P1", 130, [("VIN", "w5", "5 V from USB", True), ("TX", None, "serial console", False),
                     ("RX", None, "serial console", False), ("GND", "wg", "ground", True)]),
        ("P3", 280, [("GND", None, "ground", False), ("IO35", "wpwm", "CO₂ PWM in", True),
                     ("IO22", None, "not used", False), ("IO21", None, "backlight, leave", False)]),
        ("CN1", 430, [("GND", "wg", "ground", True), ("IO22", None, "not used", False),
                      ("IO27", "wdat", "1-Wire data", True), ("3V3", "w3", "probe supply", True)]),
    ]
    for name, top, pins in conns:
        box(700, top, 90, 130, p["block"])
        text(802, top + 24, name, "ink", weight=600, size=13)
        for i, (pin, col, note, used) in enumerate(pins):
            y = top + 20 + i * 30
            text(714, y + 4, pin, "ink" if used else "faint", weight=600 if used else None)
            text(846, y + 4, note, "dim" if used else "faint")
            pad(700, y, col if col else "line")

    text(716, 96, "USB-C: power and programming", "dim")

    # ---- legend ------------------------------------------------------------------------
    legend = [("w5", "+5 V"), ("w3", "3V3"), ("wg", "GND"), ("wpwm", "CO₂ PWM"), ("wdat", "1-Wire data")]
    x = 20
    for col, label in legend:
        wire([(x, 598), (x + 26, 598)], col, flow=False)
        text(x + 34, 602, label, "dim")
        x += 150
    a("</svg>")
    return "\n".join(o)

def search_url(q):
    return "https://www.aliexpress.com/w/wholesale-" + "-".join(q.split()) + ".html"


def annotate(name, marks):
    """Compose docs/img/<name>.png: the capture at 2x with numbered callouts in the margins."""
    from PIL import Image, ImageDraw, ImageFont
    S, M, PADY = 2, 64, 16
    raw = Image.open(os.path.join(DOCS, "img", "raw", name + ".png")).convert("RGB")
    shot = raw.resize((raw.width * S, raw.height * S), Image.NEAREST)
    W, H = shot.width + 2 * M, shot.height + 2 * PADY
    im = Image.new("RGB", (W, H), (10, 17, 18))
    im.paste(shot, (M, PADY))
    d = ImageDraw.Draw(im)
    d.rectangle([M - 1, PADY - 1, M + shot.width, PADY + shot.height], outline=(42, 61, 63))
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15)
    orange, dark = (240, 154, 46), (10, 17, 18)
    for i, (x, y, side, _) in enumerate(marks, 1):
        tx, ty = M + x * S, PADY + y * S
        cx = M // 2 if side == "L" else W - M // 2
        d.line([(cx, ty), (tx, ty)], fill=orange, width=1)
        d.ellipse([tx - 3, ty - 3, tx + 3, ty + 3], fill=orange)
        d.ellipse([cx - 12, ty - 12, cx + 12, ty + 12], fill=orange)
        t = str(i)
        bb = d.textbbox((0, 0), t, font=font)
        d.text((cx - (bb[2] - bb[0]) / 2 - bb[0], ty - (bb[3] - bb[1]) / 2 - bb[1]), t, fill=dark, font=font)
    out = os.path.join(DOCS, "img", name + ".png")
    im.save(out, optimize=True)
    return out


CSS = """
:root{--bg:#F2F6F6;--panel:#FFFFFF;--block:#E6EEEE;--line:#C3D1D2;--ink:#0E1B1D;--ink-dim:#44585A;--ink-faint:#7A8C8E;
--accent:#007078;--warm:#B5610A;--code:#E3ECEC;
--w5:#C8362E;--w3:#C96F0A;--wg:#5C6B6D;--wpwm:#007078;--wdat:#8A6D00}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){color-scheme:dark;--bg:#0A1112;--panel:#101A1B;--block:#18282A;--line:#2A3D3F;--ink:#E6EEEE;--ink-dim:#A9B8B9;--ink-faint:#6F8183;
--accent:#2BB5BC;--warm:#F09A2E;--code:#142123;
--w5:#F0625A;--w3:#F09A2E;--wg:#93A3A5;--wpwm:#2BB5BC;--wdat:#E3C341}}
:root[data-theme="dark"]{color-scheme:dark;--bg:#0A1112;--panel:#101A1B;--block:#18282A;--line:#2A3D3F;--ink:#E6EEEE;--ink-dim:#A9B8B9;--ink-faint:#6F8183;
--accent:#2BB5BC;--warm:#F09A2E;--code:#142123;
--w5:#F0625A;--w3:#F09A2E;--wg:#93A3A5;--wpwm:#2BB5BC;--wdat:#E3C341}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--ink);font:16px/1.6 "IBM Plex Sans",system-ui,sans-serif;padding-inline:20px;padding-block:40px 72px}
.wrap{max-width:1000px;margin-inline:auto;display:flex;flex-direction:column;gap:56px}
header{display:flex;flex-direction:column;gap:12px;border-bottom:2px solid var(--accent);padding-bottom:24px}
.eyebrow{font:500 12px/1 "IBM Plex Mono",ui-monospace,monospace;letter-spacing:.16em;text-transform:uppercase;color:var(--accent)}
h1,h2,h3{font-family:"Chakra Petch","IBM Plex Sans",system-ui,sans-serif;text-wrap:balance;margin:0;line-height:1.15}
h1{font-size:clamp(30px,6vw,46px);font-weight:600;letter-spacing:.01em}
h2{font-size:24px;font-weight:600}
h3{font-size:17px;font-weight:600}
p{margin:0;max-width:66ch}
.lede{font-size:18px;color:var(--ink-dim)}
section{display:flex;flex-direction:column;gap:18px}
.scroll{overflow-x:auto;border:1px solid var(--line);border-radius:8px;background:var(--panel)}
table{border-collapse:collapse;width:100%;font-size:15px}
th,td{text-align:left;padding:10px 14px;border-bottom:1px solid var(--line);vertical-align:top}
tr:last-child td{border-bottom:0}
th{font:500 11px/1.2 "IBM Plex Mono",ui-monospace,monospace;letter-spacing:.12em;text-transform:uppercase;color:var(--ink-faint)}
td.n{font-variant-numeric:tabular-nums;white-space:nowrap}
code,.pin{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.92em}
.pin{color:var(--accent);font-weight:500;white-space:nowrap}
pre.cmd{margin:0;background:var(--code);border-radius:6px;padding:12px 14px;overflow-x:auto;font:13px/1.5 "IBM Plex Mono",ui-monospace,monospace;color:var(--ink)}
figure{margin:0;display:flex;flex-direction:column;gap:10px}
figcaption{font-size:14px;color:var(--ink-dim);max-width:70ch}
.map{overflow-x:auto;border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:12px}
.map svg{display:block;min-width:820px;width:100%;height:auto;font-family:"IBM Plex Sans",system-ui,sans-serif}
ol.steps{list-style:none;margin:0;padding:0;counter-reset:s;display:flex;flex-direction:column;gap:0}
ol.steps li{counter-increment:s;display:grid;grid-template-columns:44px 1fr;gap:6px 14px;padding-block:18px;border-top:1px solid var(--line)}
ol.steps li::before{content:counter(s);font:600 22px/1.2 "Chakra Petch",sans-serif;color:var(--accent);font-variant-numeric:tabular-nums}
ol.steps li>div{display:flex;flex-direction:column;gap:8px;min-width:0}
.flows{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,430px),1fr));gap:28px}
.flow{overflow-x:auto;border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:14px}
pre.mermaid{margin:0;font:13px/1.5 "IBM Plex Mono",ui-monospace,monospace;color:var(--ink-dim)}
.note{border-left:3px solid var(--warm);padding:4px 0 4px 14px;color:var(--ink-dim)}
dl{margin:0;display:flex;flex-direction:column;gap:14px}
dt{font-weight:600}
dd{margin:2px 0 0;color:var(--ink-dim);max-width:70ch}
.hero{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,300px),1fr));gap:16px;align-items:start}
.photo{margin:0;display:flex;flex-direction:column;gap:8px}
.photo img{display:block;width:100%;height:auto;border-radius:8px;border:1px solid var(--line)}
ol.steps .photo{max-width:520px}
.screens{display:flex;flex-direction:column;gap:40px}
.screen{display:grid;grid-template-columns:minmax(0,3fr) minmax(0,2fr);gap:14px 28px;align-items:start}
.screen h3,.screen>p{grid-column:1/-1}
.screen img{display:block;width:100%;height:auto;border-radius:8px;image-rendering:pixelated}
.screen ol{margin:0;padding:0;list-style:none;counter-reset:c;display:flex;flex-direction:column;gap:10px;font-size:15px}
.screen li{counter-increment:c;display:grid;grid-template-columns:26px 1fr;gap:10px;align-items:start}
.screen li::before{content:counter(c);width:24px;height:24px;border-radius:50%;background:#F09A2E;color:#0A1112;font:600 13px/24px "IBM Plex Sans",sans-serif;text-align:center;font-variant-numeric:tabular-nums}
@media (max-width:720px){.screen{grid-template-columns:minmax(0,1fr)}}
a{color:var(--accent);text-underline-offset:3px}
a:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
footer{font-size:13px;color:var(--ink-faint);border-top:1px solid var(--line);padding-top:16px}
"""


# ------------------------------------------------------------------ content --
PARTS = [
 ("ESP32-2432S028R display board", "1", "Usually sold with its JST pigtails", "ESP32-2432S028R"),
 ("Winsen MH-Z16, 0–10 % vol", "1", "Must be the 0–10 % range", "MH-Z16 CO2 sensor"),
 ("DS18B20 waterproof probe", "1", "Red, black and yellow leads", "DS18B20 waterproof probe"),
 ("DS18B20 screw-terminal adapter", "1", "Carries the pull-up resistor", "DS18B20 adapter module terminal"),
 ("Lever connectors", "3", "One per MH-Z16 lead", "lever wire connector 2 pin"),
 ("Acrylic case for the board", "1", "", "ESP32-2432S028R case"),
 ("USB-C cable and 5 V supply", "1", "", "USB-C cable"),
 ("Small box, pipette tip box", "1 each", "Junction box and sensor holder", None),
 ("microSD card", "optional", "For logging", "microSD card 8GB"),
]

INTRO = [
 ("The parts are cheap", "The whole build costs a small fraction of a commercial incubator CO₂ monitor."),
 ("Ready-made meters will not do", "Most ready-made CO₂ meters, including those on AliExpress, stop at 5,000 ppm. An incubator runs at 50,000 ppm, so they are useless here. The MH-Z16 measures up to 100,000 ppm."),
 ("Calibrate before you trust it", "Out of the box this sensor read about a third high inside the incubator. Compare it with a known incubator or a trusted meter first."),
 ("Not tied to this board", "The sensor needs one input pin, so any ESP32 will do, with or without a screen. Other sensors, such as humidity, can be added."),
]

PINS = [
 ("MH-Z16 +5 V", "P1", "VIN"),
 ("MH-Z16 GND", "P1", "GND"),
 ("MH-Z16 PWM", "P3", "IO35"),
 ("DS18B20 VCC, red", "CN1", "3V3"),
 ("DS18B20 GND, black", "CN1", "GND"),
 ("DS18B20 DATA, yellow", "CN1", "IO27"),
]

STEPS = [
 ("Flash the firmware", "Do this on the bare board, before any wiring.",
  "arduino-cli lib install \"LovyanGFX\" \"OneWire\" \"DallasTemperature\"\ncd firmware/CO2_CYD\n./build.sh upload", []),
 ("Fit the pigtails", "Plug a JST pigtail into P1, P3 and CN1. Jumper pins do not grip these sockets.", None,
  [("pigtails", "Pigtails in place on the cased board.")]),
 ("Wire the MH-Z16", "Red to VIN, black to GND, yellow to IO35, through the lever connectors. Tie back the other leads.", None,
  [("mhz16-leads", "Three leads in use, the rest tied back.")]),
 ("Wire the DS18B20", "Clamp the probe into the adapter. Run VCC to 3V3, GND to GND, DAT to IO27.", None, []),
 ("Close the junction box", "Sensor leads in one side, board wires out the other.", None,
  [("junction", "Probe adapter on the left, lever connectors on the right.")]),
 ("Power up", "The start-up screen lists each part as it is found. Expect CO₂ SENSOR ONLINE and TEMP PROBE OK.", None, []),
 ("Calibrate the touch panel", "Open the serial monitor, type c, tap the four targets.", "cd firmware/CO2_CYD\n./build.sh monitor", []),
 ("Place the sensor head", "Sensors inside the incubator, board and junction box outside.", None,
  [("sensor-head", "A pipette tip box holds both sensors."),
   ("cable-run", "The cable comes out of the chamber and is strapped to the side panel.")]),
]

SCREENS = [
 ("main", "Main page", [
   (316, 14, "R", "LIVE while readings arrive"),
   (60, 52, "L", "Gauge, 0 to 10 %, with the target band shaded"),
   (70, 78, "L", "CO₂ in percent"),
   (104, 110, "L", "CO₂ in ppm, raw sensor value in brackets, then the trend"),
   (316, 150, "R", "Status"),
   (12, 224, "L", "Last 15 minutes: CO₂ in teal, temperature in orange"),
   (310, 196, "R", "Temperature"),
 ]),
 ("graph", "Graph page", [
   (12, 45, "L", "CO₂ axis"),
   (305, 60, "R", "Temperature axis"),
   (5, 182, "L", "Now, minimum, maximum and average"),
   (314, 220, "R", "Time span"),
 ]),
]

STATUS_FLOW = f'''flowchart TD
  A{{"Valid reading in<br/>the last 10 s?"}}
  A -- no --> E["SENSOR ERROR"]
  A -- yes --> D{{"Sensor warm?"}}
  D -- no --> W["WARMING UP"]
  D -- yes --> O{{"Raw reading at or<br/>above {OVER} ppm?"}}
  O -- yes --> OR["OVER RANGE"]
  O -- no --> F{{"Corrected CO2"}}
  F -- "below 4.5 %" --> L["LOW CO2"]
  F -- "4.5 to 5.5 %" --> N["NORMAL"]
  F -- "above 5.5 %" --> H["HIGH CO2"]'''

CAL_STEPS = [
 "Let the monitor run in an incubator at a known setpoint for three days. The reading climbs while the sensor settles.",
 "Read the raw value in brackets on the main page.",
 "Divide the known value by the raw value. For 5.0 % known and 6.7 % raw that is 0.75.",
 "Set CO2_CAL_GAIN to that number in Config.h and flash again.",
]
CAL_NOTE = ("The firmware ships uncorrected, with a gain of 1.0. The unit in these photos needed 0.72. "
            "Repeat the check from time to time.")

CHECKS = [
 ("PWM period", "about 1001 ms", "Not an MH-Z16 signal"),
 ("edges/s", "2.0", "Near 0: PWM wire is off"),
 ("valid/rej", "rejected stays at 0", "Loose PWM or GND wire"),
 ("DS18B20", "ok 12-bit", "Probe not found at power-on"),
]

TROUBLE = [
 ("Shows about 5 % at power-on", "Normal. The sensor reports 50,000 ppm while it preheats."),
 ("Temperature shows n/a", "Check the adapter's screw terminals, then power-cycle."),
 ("Taps land in the wrong place", "Run the touch calibration again."),
 ("Cannot join the lab network", f"After 60 seconds it starts its own network, {AP_SSID}. Join it and open http://192.168.4.1/."),
]

HERO_CAPTIONS = [
 "Calibrated and reading inside the target band.",
 "In place beside the incubator, before calibration. It read 6.66 % in a 5 % chamber.",
]
REPO_URL = "https://github.com/tggr-lab/incubator-co2-monitor"
LICENCE_TEXT = "Code under the MIT licence. Guide, photos and data under CC BY 4.0."

def photo_uri(name):
    return "data:image/jpeg;base64," + base64.b64encode(open(os.path.join(DOCS, "photos", name + ".jpg"), "rb").read()).decode()

def e(s): return html.escape(s, quote=False)

CSS += """
.points{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,260px),1fr));gap:20px 28px}
.points h3{color:var(--accent);margin-bottom:6px}
.points p{color:var(--ink-dim)}
.pair{display:flex;flex-wrap:wrap;gap:14px;align-items:flex-start}
.pair .photo{flex:0 1 320px;max-width:320px}
ol.cal{margin:0;padding-left:22px;display:flex;flex-direction:column;gap:6px;max-width:66ch}
.wiredot{fill:none;stroke:var(--ink);stroke-width:3;stroke-linecap:round;stroke-dasharray:1 17;animation:flow 1.1s linear infinite}
.wiredot.rev{animation-direction:reverse}
@keyframes flow{to{stroke-dashoffset:-18}}
@media (prefers-reduced-motion:reduce){.wiredot{display:none}}
"""

PWM_SVG = """<svg viewBox="0 0 720 210" role="img" aria-label="The sensor's output is high for a share of each one second cycle, and that share grows with the CO2 level">
<line x1="40" y1="130" x2="680" y2="130" style="stroke:var(--line);stroke-width:1.5"/>
<path id="pwm-wave" d="M40,130 V50 H190 V130 H340 V50 H490 V130 H640" style="fill:none;stroke:var(--wpwm);stroke-width:3;stroke-linejoin:round"/>
<rect id="pwm-fill" x="40" y="50" width="150" height="80" style="fill:var(--wpwm);opacity:.18"/>
<line x1="40" y1="150" x2="340" y2="150" style="stroke:var(--ink-faint);stroke-width:1.5"/>
<line x1="40" y1="144" x2="40" y2="156" style="stroke:var(--ink-faint);stroke-width:1.5"/>
<line x1="340" y1="144" x2="340" y2="156" style="stroke:var(--ink-faint);stroke-width:1.5"/>
<text x="190" y="170" text-anchor="middle" font-size="12" style="fill:var(--ink-dim)">one cycle, 1004 ms</text>
<text x="40" y="34" font-size="13" font-weight="600" style="fill:var(--ink)"><tspan id="pwm-th">High for 502 ms</tspan></text>
<text x="680" y="34" text-anchor="end" font-size="13" font-weight="600" style="fill:var(--accent)"><tspan id="pwm-val">5.00 % CO₂</tspan></text>
<text x="40" y="198" font-size="12" style="fill:var(--ink-dim)">ppm = 100000 × (high − 2 ms) / (cycle − 4 ms)</text>
</svg>"""

PWM_JS = """<script>
(function(){
  var w=document.getElementById('pwm-wave'),f=document.getElementById('pwm-fill'),
      th=document.getElementById('pwm-th'),v=document.getElementById('pwm-val');
  if(!w||matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  var LO=600,HI=50000,P=300;
  function draw(ppm){
    var ms=2+ppm/100,d=ms/1004*P,a=40+d,b=340+d;
    w.setAttribute('d','M40,130 V50 H'+a.toFixed(1)+' V130 H340 V50 H'+b.toFixed(1)+' V130 H640');
    f.setAttribute('width',d.toFixed(1));
    th.textContent='High for '+(ms<20?ms.toFixed(1):Math.round(ms))+' ms';
    var pct=ppm/10000;v.textContent=(pct>=1?pct.toFixed(2):pct.toFixed(3))+' % CO₂';
  }
  function ease(x){return x*x*(3-2*x)}
  function frame(t){
    var s=(t/1000)%9,k;
    if(s<1.5)k=0;else if(s<4)k=ease((s-1.5)/2.5);else if(s<6.5)k=1;else k=1-ease((s-6.5)/2.5);
    draw(LO+(HI-LO)*k);requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
</script>"""

# ------------------------------------------------------- an hour of real data --
import csv, json, statistics, math

def load_hour():
    rows = list(csv.DictReader(open(os.path.join(DOCS, "data", "incubator-hour.csv"))))
    span = int(rows[0]["seconds_ago"])
    t = [span - int(r["seconds_ago"]) for r in rows]
    return (t, [float(r["co2_ppm"]) / 10000 for r in rows],
            [float(r["temp_c"]) for r in rows], [int(r["event"]) for r in rows])

def hour_stats(t, pct, temp, ev):
    first_drop = next(i for i, v in enumerate(pct) if v < 4.9)
    rest = pct[:first_drop - 1]
    half = len(pct) // 2
    lo2 = min(range(half, len(pct)), key=lambda i: pct[i])
    lo1 = min(range(0, half), key=lambda i: pct[i])
    after = lambda i0, thr: next(t[j] for j in range(i0, len(pct)) if pct[j] >= thr) - t[i0]
    # the last low point of the first burst, where an uninterrupted climb begins
    recA = min((i for i in range(len(t)) if 1100 <= t[i] <= 1300), key=lambda i: pct[i])
    return dict(
        minutes=t[-1] / 60, noise_ppm=statistics.pstdev(rest) * 10000, rest=statistics.mean(rest),
        fall=-min(pct[i + 6] - pct[i] for i in range(len(pct) - 6)),
        lo1=lo1, lo2=lo2, low=min(pct), back=after(lo2, 4.5) / 60, settle=after(lo2, 4.95) / 60,
        below=sum(1 for v in pct if v < 4.5) * 10 / 60, doors=sum(ev), end=pct[-1],
        tmin=min(temp), tmax=max(temp), tend=temp[-1], recA=recA)

HOUR = load_hour()
HS = hour_stats(*HOUR)

CHART_LIGHT = dict(CSS_PALETTE, s1="var(--s1)", s2="var(--s2)")
CHART_DARK = dict(DARK_PALETTE, s1="#1FA3AA", s2="#D47A18")

class Svg:
    def __init__(self, w, h, label, p, standalone):
        self.p, self.o = p, []
        font = ' font-family="IBM Plex Sans, system-ui, sans-serif"' if standalone else ""
        ns = ' xmlns="http://www.w3.org/2000/svg"' if standalone else ""
        self.o.append(f'<svg{ns} viewBox="0 0 {w} {h}" role="img"{font} font-size="12" aria-label="{label}">')
        if standalone:
            self.o.append(f'<rect width="{w}" height="{h}" style="fill:{p["ground"]}"/>')
    def text(self, x, y, s, col="dim", anchor="start", weight=None):
        ex = (f' text-anchor="{anchor}"' if anchor != "start" else "") + (f' font-weight="{weight}"' if weight else "")
        self.o.append(f'<text x="{x:.1f}" y="{y:.1f}"{ex} style="fill:{self.p[col]}">{s}</text>')
    def hline(self, x0, x1, y):
        self.o.append(f'<line x1="{x0}" y1="{y:.1f}" x2="{x1}" y2="{y:.1f}" style="stroke:{self.p["line"]};stroke-width:1"/>')
    def rect(self, x, y, w, h, col, op):
        self.o.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" style="fill:{self.p[col]};opacity:{op}"/>')
    def line(self, pts, col):
        s = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        self.o.append(f'<polyline points="{s}" style="fill:none;stroke:{self.p[col]};stroke-width:2;stroke-linejoin:round;stroke-linecap:round"/>')
    def area(self, pts, base, col):
        s = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts) + f" {pts[-1][0]:.1f},{base} {pts[0][0]:.1f},{base}"
        self.o.append(f'<polygon points="{s}" style="fill:{self.p[col]};opacity:.10"/>')
    def dot(self, x, y, col):
        ring = self.p["panel"] if self.p["ground"] == "none" else self.p["ground"]
        self.o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" style="fill:{self.p[col]};stroke:{ring};stroke-width:2"/>')
    def tri(self, x, y):
        self.o.append(f'<polygon points="{x-4:.1f},{y} {x+4:.1f},{y} {x:.1f},{y+7}" style="fill:{self.p["dim"]}"/>')
    def done(self):
        return "\n".join(self.o + ["</svg>"])

def chart_hour(p, standalone):
    t, pct, temp, ev = HOUR
    X0, X1, AT, AB, BT, BB = 56, 880, 36, 290, 330, 430
    sx = lambda s: X0 + (X1 - X0) * s / t[-1]
    ya = lambda v: AB - (AB - AT) * v / 6.0
    yb = lambda v: BB - (BB - BT) * (v - 37.0) / 1.5
    g = Svg(960, 470, "Carbon dioxide and temperature over one hour, with two bursts of door openings", p, standalone)
    g.text(X0, 22, "CO₂, %", "ink", weight=600)
    for v in range(0, 7):
        g.hline(X0, X1, ya(v)); g.text(X0 - 8, ya(v) + 4, str(v), "faint", "end")
    g.rect(X0, ya(5.5), X1 - X0, ya(4.5) - ya(5.5), "s1", .10)
    g.text(X1, ya(5.5) - 6, "target band, 4.5 to 5.5 %", "dim", "end")
    a_pts = [(sx(s), ya(v)) for s, v in zip(t, pct)]
    g.area(a_pts, AB, "s1"); g.line(a_pts, "s1")
    first = True
    for s, e_ in zip(t, ev):
        if e_:
            g.tri(sx(s), AT + 2)
            if first: g.text(sx(s) - 4, AT - 8, "door opened", "dim"); first = False
    for i in (HS["lo1"], HS["lo2"]):
        g.dot(sx(t[i]), ya(pct[i]), "s1"); g.text(sx(t[i]), ya(pct[i]) + 19, f"{pct[i]:.2f} %", "ink", "middle")
    g.dot(a_pts[-1][0], a_pts[-1][1], "s1"); g.text(X1 + 10, a_pts[-1][1] + 4, f"{pct[-1]:.2f} %", "ink")

    g.text(X0, BT - 12, "Temperature, °C", "ink", weight=600)
    for v in (37.0, 37.5, 38.0, 38.5):
        g.hline(X0, X1, yb(v)); g.text(X0 - 8, yb(v) + 4, f"{v:.1f}", "faint", "end")
    b_pts = [(sx(s), yb(v)) for s, v in zip(t, temp)]
    g.line(b_pts, "s2"); g.dot(b_pts[-1][0], b_pts[-1][1], "s2")
    g.text(X1 + 10, b_pts[-1][1] + 4, f"{temp[-1]:.1f} °C", "ink")
    for m in range(0, 61, 10):
        g.text(sx(m * 60), BB + 18, str(m), "faint", "middle")
    g.text(X1, BB + 36, "minutes", "faint", "end")
    cfg = dict(W=960, x0=X0, x1=X1, top=AT, bottom=BB, xs=[round(s / 60, 2) for s in t], xunit="min",
               series=[dict(name="CO₂", unit="%", dec=2, vals=pct, vmin=0, vmax=6, ytop=AT, ybot=AB, color="--s1"),
                       dict(name="Temperature", unit="°C", dec=1, vals=temp, vmin=37.0, vmax=38.5, ytop=BT, ybot=BB, color="--s2")])
    return g.done(), cfg

def chart_recovery(p, standalone):
    t, pct, temp, ev = HOUR
    X0, X1, T, B, N = 56, 820, 30, 280, 61
    a0, b0 = HS["recA"], HS["lo2"]
    A, Bv = pct[a0:a0 + N], pct[b0:b0 + N]
    sx = lambda i: X0 + (X1 - X0) * i / (N - 1)
    sy = lambda v: B - (B - T) * v / 6.0
    g = Svg(960, 330, "Two recoveries after the door closed, both reaching the target band within five minutes", p, standalone)
    for v in range(0, 7):
        g.hline(X0, X1, sy(v)); g.text(X0 - 8, sy(v) + 4, str(v), "faint", "end")
    g.text(X0, 18, "CO₂, %", "ink", weight=600)
    g.rect(X0, sy(5.5), X1 - X0, sy(4.5) - sy(5.5), "s1", .10)
    g.text(X1, sy(5.5) - 6, "target band", "dim", "end")
    for vals, col, lab in ((A, "s2", f"from {A[0]:.2f} %"), (Bv, "s1", f"from {Bv[0]:.2f} %")):
        pts = [(sx(i), sy(v)) for i, v in enumerate(vals)]
        g.line(pts, col); g.dot(pts[0][0], pts[0][1], col)
        g.text(pts[0][0] + 12, pts[0][1] + 16, lab, "ink")
    for m in range(0, 11, 2):
        g.text(sx(m * 6), B + 18, str(m), "faint", "middle")
    g.text(X1, B + 36, "minutes after the lowest point", "faint", "end")
    cfg = dict(W=960, x0=X0, x1=X1, top=T, bottom=B, xs=[round(i / 6, 2) for i in range(N)], xunit="min",
               series=[dict(name=f"From {A[0]:.2f} %", unit="%", dec=2, vals=A, vmin=0, vmax=6, ytop=T, ybot=B, color="--s2"),
                       dict(name=f"From {Bv[0]:.2f} %", unit="%", dec=2, vals=Bv, vmin=0, vmax=6, ytop=T, ybot=B, color="--s1")])
    return g.done(), cfg

TILES = [
 ("Noise at rest", f"±{HS['noise_ppm']:.0f} ppm"),
 ("Fastest fall", f"{HS['fall']:.1f} % per min"),
 ("Back in band", f"{HS['back']:.1f} min"),
 ("Below the band", f"{HS['below']:.0f} of {HS['minutes']:.0f} min"),
]
FINDINGS = [
 f"The door was opened in two bursts. Each time CO₂ fell from {HS['rest']:.1f} % to about {HS['low']:.1f} %.",
 f"After the last opening the incubator brought CO₂ back into the band in {HS['back']:.1f} minutes and settled in {HS['settle']:.1f}.",
 f"Temperature stayed between {HS['tmin']:.1f} and {HS['tmax']:.1f} °C throughout.",
]

def chart_block(cid, svg, cfg, replay=False):
    btn = f'<button class="replay" id="{cid}-replay" type="button">Replay the hour</button>' if replay else ""
    return (f'{btn}<div class="map"><div class="chart" id="{cid}" tabindex="0">{svg}<div class="tip" hidden></div></div></div>'
            f'<script type="application/json" id="{cid}-cfg">{json.dumps(cfg)}</script>')

def section_hour():
    t, pct, temp, ev = HOUR
    o = ['<section id="hour"><h2>An hour in the incubator</h2>'
         f'<p>A log from the monitor itself: {len(t)} readings, ten seconds apart.</p><div class="tiles">']
    o += [f'<div class="tile"><small>{e(k)}</small><b>{e(v)}</b></div>' for k, v in TILES]
    o.append('</div>')
    svg, cfg = chart_hour(CHART_LIGHT, False)
    o.append('<figure>' + chart_block("c-hour", svg, cfg, True) +
             '<figcaption>Move the pointer over the chart, or use the arrow keys, to read any moment.</figcaption></figure>')
    o.append('<ul class="find">' + "".join(f"<li>{e(f)}</li>" for f in FINDINGS) + '</ul>')
    svg, cfg = chart_recovery(CHART_LIGHT, False)
    o.append('<figure><h3>Recovery after the door closes</h3>'
             '<div class="legend"><span><i style="background:var(--s2)"></i>' + e(cfg["series"][0]["name"]) +
             '</span><span><i style="background:var(--s1)"></i>' + e(cfg["series"][1]["name"]) + '</span></div>' +
             chart_block("c-rec", svg, cfg) +
             '<figcaption>The deeper the drop, the longer the climb. Both are back in the band within five minutes.</figcaption></figure>')
    o.append('<details><summary>Show the data as a table</summary><div class="scroll tbl"><table><thead><tr>'
             '<th>Minute</th><th>CO₂, %</th><th>Temperature, °C</th><th>Door</th></tr></thead><tbody>')
    for i in range(0, len(t), 6):
        door = "opened" if any(ev[i:i + 6]) else ""
        o.append(f'<tr><td class="n">{t[i] // 60}</td><td class="n">{pct[i]:.2f}</td><td class="n">{temp[i]:.1f}</td><td>{door}</td></tr>')
    o.append('</tbody></table></div></details></section>')
    return "\n".join(o)

def md_hour():
    hour, _ = chart_hour(CHART_DARK, True); rec, _ = chart_recovery(CHART_DARK, True)
    open(os.path.join(DOCS, "img", "hour.svg"), "w").write(hour + "\n")
    open(os.path.join(DOCS, "img", "recovery.svg"), "w").write(rec + "\n")
    o = ["## An hour in the incubator", "",
         f"A log from the monitor itself: {len(HOUR[0])} readings, ten seconds apart. "
         "The data is in [data/incubator-hour.csv](data/incubator-hour.csv).", "",
         "| " + " | ".join(k for k, _ in TILES) + " |", "|" + "---|" * len(TILES),
         "| " + " | ".join(v for _, v in TILES) + " |", "",
         "![CO₂ and temperature over one hour](img/hour.svg)", ""]
    o += [f"- {f}" for f in FINDINGS]
    o += ["", "### Recovery after the door closes", "", "![Two recoveries after the door closed](img/recovery.svg)", "",
          "Orange is the recovery from the shallower drop, teal from the deeper one.", ""]
    return o

# ------------------------------------------------------------ restored flows --
BOOT_FLOW = '''flowchart TD
  A["Power on"] --> B["Start the display"]
  B --> C["Load touch calibration"]
  C --> D["Start CO2 capture on IO35"]
  D --> E["Find the probe on IO27"]
  E --> F["Look for SD card and Wi-Fi"]
  F --> G{"First CO2 reading in,<br/>or 12 s passed?"}
  G -- no --> G
  G -- yes --> H["Main page"]'''

MEASURE_FLOW = f'''flowchart TD
  A["Pulse edge on IO35"] --> B{{"Glitch, under 200 µs?"}}
  B -- yes --> X["Ignore"]
  B -- no --> C["Time the full cycle"]
  C --> D{{"Cycle 900 to 1100 ms<br/>and timings agree?"}}
  D -- no --> R["Reject, keep the last value"]
  D -- yes --> E["Convert pulse length to ppm"]
  E --> F["Average 8 cycles"]
  F --> K["Apply your calibration factor"]
  K --> G{{"Sensor warm?"}}
  G -- no --> H["Show only"]
  G -- yes --> I["Show, log, watch for door events"]'''

FLOW_SET = [("From power-on to the main page", BOOT_FLOW),
            ("How one reading is made", MEASURE_FLOW),
            ("How the status is decided", STATUS_FLOW)]

def flows_html():
    return ('<div class="flows">' + "".join(
        f'<figure><h3>{e(t)}</h3><div class="scroll" style="padding:14px"><pre class="mermaid">{e(src)}</pre></div></figure>'
        for t, src in FLOW_SET) + '</div>')

def md_flows():
    o = []
    for t, src in FLOW_SET: o += [f"### {t}", "", "```mermaid", src, "```", ""]
    return o

# ------------------------------------------------------------- web dashboard --
DASH_TEXT = ("Open the Connect page on the monitor and scan its QR codes with a phone. "
             "One joins the monitor's own network and the other opens the page. "
             "The page shows the live reading and the history, sets the monitor's clock from the phone, "
             "and offers every log file on the SD card for download.")
LOG_TEXT = "The monitor starts a new numbered log file at each power-on, such as co2_0007.csv, with one row every ten seconds."

def section_dashboard():
    uri = "data:image/png;base64," + base64.b64encode(open(os.path.join(DOCS, "photos", "dashboard.png"), "rb").read()).decode()
    return ('<section id="dashboard"><h2>Web dashboard</h2>' + f'<p>{e(DASH_TEXT)}</p><p>{e(LOG_TEXT)}</p>'
            f'<figure class="photo" style="max-width:720px"><img src="{uri}" width="977" height="1013" '
            'alt="The web dashboard showing 5.02 percent, status NORMAL, a history chart and two log files">'
            '<figcaption>The dashboard after a door opening, with CO₂ back at 5.02 %.</figcaption></figure></section>')

def md_dashboard():
    return ["## Web dashboard", "", DASH_TEXT, "", LOG_TEXT, "", "![The web dashboard](photos/dashboard.png)", ""]

CSS += """
:root{--s1:#008C99;--s2:#B5610A}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--s1:#1FA3AA;--s2:#D47A18}}
:root[data-theme="dark"]{--s1:#1FA3AA;--s2:#D47A18}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.tile{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;display:flex;flex-direction:column;gap:4px}
.tile small{font:500 11px/1.2 "IBM Plex Mono",ui-monospace,monospace;letter-spacing:.1em;text-transform:uppercase;color:var(--ink-faint)}
.tile b{font-size:20px;font-weight:600}
.chart{position:relative;min-width:820px;outline:none}
.chart:focus-visible{outline:2px solid var(--accent);outline-offset:4px}
.chart svg{cursor:crosshair}
.tip{position:absolute;top:6px;pointer-events:none;background:var(--bg);border:1px solid var(--line);border-radius:6px;padding:8px 10px;font-size:13px;line-height:1.5;white-space:nowrap;box-shadow:0 2px 10px rgba(0,0,0,.18)}
.tip .when{color:var(--ink-faint);font-size:12px}
.tip .row{display:flex;align-items:center;gap:8px}
.tip .row i{width:12px;height:2px;display:inline-block}
.tip .row b{font-weight:600;font-variant-numeric:tabular-nums}
.tip .row span{color:var(--ink-dim)}
.legend{display:flex;gap:18px;flex-wrap:wrap;font-size:13px;color:var(--ink-dim)}
.legend i{display:inline-block;width:16px;height:2px;vertical-align:middle;margin-right:6px}
.replay{align-self:flex-start;font:500 13px "IBM Plex Sans",sans-serif;color:var(--ink);background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:7px 14px;cursor:pointer}
.replay:hover{border-color:var(--accent)}
.replay:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
ul.find{margin:0;padding-left:20px;display:flex;flex-direction:column;gap:6px;max-width:68ch}
details summary{cursor:pointer;color:var(--accent);font-weight:500}
.tbl{max-height:320px;overflow:auto;margin-top:10px}
.flows{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,300px),1fr));gap:24px;align-items:start}
@media (prefers-reduced-motion:reduce){.replay{display:none}}
"""

CHART_JS = """<script>
(function(){
  var NS='http://www.w3.org/2000/svg';
  function setup(id){
    var box=document.getElementById(id),cfgEl=document.getElementById(id+'-cfg');
    if(!box||!cfgEl) return null;
    var c=JSON.parse(cfgEl.textContent),svg=box.querySelector('svg'),tip=box.querySelector('.tip');
    var n=c.xs.length,cur=-1,timer=null;
    var hair=document.createElementNS(NS,'line');
    hair.setAttribute('y1',c.top);hair.setAttribute('y2',c.bottom);
    hair.setAttribute('style','stroke:var(--ink-faint);stroke-width:1');hair.setAttribute('visibility','hidden');
    svg.appendChild(hair);
    var dots=c.series.map(function(s){
      var d=document.createElementNS(NS,'circle');d.setAttribute('r',4.5);
      d.setAttribute('style','fill:var('+s.color+');stroke:var(--panel);stroke-width:2');
      d.setAttribute('visibility','hidden');svg.appendChild(d);return d;});
    var xa=c.hours!=null?0:c.xs[0],xb=c.hours!=null?c.hours:c.xs[n-1];
    function X(i){return c.x0+(c.x1-c.x0)*(c.xs[i]-xa)/((xb-xa)||1)}
    function show(i){
      i=Math.max(0,Math.min(n-1,i));cur=i;var x=X(i);
      hair.setAttribute('x1',x);hair.setAttribute('x2',x);hair.setAttribute('visibility','visible');
      while(tip.firstChild) tip.removeChild(tip.firstChild);
      var w=document.createElement('div');w.className='when';
      w.textContent=c.xlabels?c.xlabels[i]:c.xs[i].toFixed(1)+' '+c.xunit;tip.appendChild(w);
      c.series.forEach(function(s,k){
        var v=s.vals[i],y=s.ybot-(s.ybot-s.ytop)*(v-s.vmin)/(s.vmax-s.vmin);
        dots[k].setAttribute('cx',x);dots[k].setAttribute('cy',y);dots[k].setAttribute('visibility','visible');
        var r=document.createElement('div');r.className='row';
        var key=document.createElement('i');key.style.background='var('+s.color+')';
        var b=document.createElement('b');b.textContent=v.toFixed(s.dec)+' '+s.unit;
        var nm=document.createElement('span');nm.textContent=s.name;
        r.appendChild(key);r.appendChild(b);r.appendChild(nm);tip.appendChild(r);
      });
      tip.hidden=false;
      var f=x/c.W;tip.style.left=(f*100)+'%';
      tip.style.transform=f>0.6?'translateX(calc(-100% - 14px))':'translateX(14px)';
    }
    function hide(){hair.setAttribute('visibility','hidden');dots.forEach(function(d){d.setAttribute('visibility','hidden')});tip.hidden=true;cur=-1}
    function stop(){if(timer){cancelAnimationFrame(timer);timer=null}}
    svg.addEventListener('pointermove',function(ev){
      stop();var r=svg.getBoundingClientRect(),vx=(ev.clientX-r.left)/r.width*c.W;
      var best=0,bd=1e9;for(var k=0;k<n;k++){var d=Math.abs(X(k)-vx);if(d<bd){bd=d;best=k}}show(best);});
    svg.addEventListener('pointerleave',function(){if(!timer)hide()});
    box.addEventListener('keydown',function(ev){
      if(ev.key==='ArrowRight'||ev.key==='ArrowLeft'){stop();ev.preventDefault();
        show((cur<0?0:cur)+(ev.key==='ArrowRight'?1:-1)*(ev.shiftKey?6:1));}
      if(ev.key==='Escape')hide();});
    box.addEventListener('blur',function(){if(!timer)hide()});
    return {play:function(ms){
      stop();var t0=null;
      function step(ts){if(t0===null)t0=ts;var k=(ts-t0)/ms;
        if(k>=1){show(n-1);timer=null;return}
        show(Math.round(k*(n-1)));timer=requestAnimationFrame(step);}
      timer=requestAnimationFrame(step);}};
  }
  function setupBars(id){
    var box=document.getElementById(id),cfgEl=document.getElementById(id+'-cfg');
    if(!box||!cfgEl) return;
    var c=JSON.parse(cfgEl.textContent),svg=box.querySelector('svg'),tip=box.querySelector('.tip'),cur=-1;
    var hi=document.createElementNS(NS,'rect');
    hi.setAttribute('y',c.top);hi.setAttribute('height',c.bottom-c.top);
    hi.setAttribute('style','fill:var(--ink);opacity:.06');hi.setAttribute('visibility','hidden');
    svg.insertBefore(hi,svg.firstChild);
    function show(i){
      i=Math.max(0,Math.min(c.bars.length-1,i));cur=i;var b=c.bars[i];
      hi.setAttribute('x',b.x);hi.setAttribute('width',b.w);hi.setAttribute('visibility','visible');
      while(tip.firstChild) tip.removeChild(tip.firstChild);
      var w=document.createElement('div');w.className='when';w.textContent=b.label;tip.appendChild(w);
      var r=document.createElement('div');r.className='row';
      var v=document.createElement('b');v.textContent=b.value+' '+c.unit;r.appendChild(v);tip.appendChild(r);
      tip.hidden=false;var f=(b.x+b.w/2)/c.W;tip.style.left=(f*100)+'%';
      tip.style.transform=f>0.6?'translateX(calc(-100% - 14px))':'translateX(14px)';
    }
    function hide(){hi.setAttribute('visibility','hidden');tip.hidden=true;cur=-1}
    svg.addEventListener('pointermove',function(ev){
      var r=svg.getBoundingClientRect(),vx=(ev.clientX-r.left)/r.width*c.W;
      show(Math.floor((vx-c.bars[0].x)/c.bars[0].w));});
    svg.addEventListener('pointerleave',hide);
    box.addEventListener('keydown',function(ev){
      if(ev.key==='ArrowRight'||ev.key==='ArrowLeft'){ev.preventDefault();show((cur<0?0:cur)+(ev.key==='ArrowRight'?1:-1));}
      if(ev.key==='Escape')hide();});
    box.addEventListener('blur',hide);
  }
  var hour=setup('c-hour');setup('c-rec');setup('c-days');setup('c-settle');
  var btn=document.getElementById('c-hour-replay');
  if(btn&&hour) btn.addEventListener('click',function(){hour.play(9000)});
})();
</script>"""

# ----------------------------------------------------------- the long run --
import datetime as _dt

def load_long():
    rows = list(csv.DictReader(open(os.path.join(DOCS, "data", "long-run-hourly.csv"))))
    summ = json.load(open(os.path.join(DOCS, "data", "long-run-summary.json")))
    return rows, summ

LONG, LS = load_long()
_q = [(int(r["hour"]), float(r["quiet_median_pct"])) for r in LONG if r["quiet_median_pct"]]
SETTLE_START = _q[0][1]
SETTLE_END = statistics.median(v for h, v in _q if h >= 72)

def _day_ticks(rows):
    out = []
    for r in rows:
        d = _dt.datetime.strptime(r["local_time"], "%Y-%m-%dT%H:%M")
        if d.hour == 0: out.append((int(r["hour"]), f"{d.day} {d.strftime('%b')}"))
    return out

def chart_days(p, standalone):
    X0, X1, T, B = 56, 880, 36, 290
    n = int(LONG[-1]["hour"])
    sx = lambda h: X0 + (X1 - X0) * h / n
    sy = lambda v: B - (B - T) * v / 6.0
    g = Svg(960, 340, "Hourly carbon dioxide over ten days, steady near five percent with dips on working days", p, standalone)
    g.text(X0, 22, "CO₂, %", "ink", weight=600)
    for v in range(0, 7):
        g.hline(X0, X1, sy(v)); g.text(X0 - 8, sy(v) + 4, str(v), "faint", "end")
    hs = [int(r["hour"]) for r in LONG]
    lo = [float(r["co2_min_pct"]) for r in LONG]; hi = [float(r["co2_max_pct"]) for r in LONG]
    med = [float(r["co2_median_pct"]) for r in LONG]
    band = " ".join(f"{sx(h):.1f},{sy(v):.1f}" for h, v in zip(hs, hi)) + " " + \
           " ".join(f"{sx(h):.1f},{sy(v):.1f}" for h, v in reversed(list(zip(hs, lo))))
    g.o.append(f'<polygon points="{band}" style="fill:{p["s1"]};opacity:.22"/>')
    g.line([(sx(h), sy(v)) for h, v in zip(hs, med)], "s1")
    g.dot(sx(hs[-1]), sy(med[-1]), "s1"); g.text(X1 + 10, sy(med[-1]) + 4, f"{med[-1]:.2f} %", "ink")
    k = min(range(len(lo)), key=lambda i: lo[i])
    g.text(sx(hs[k]), sy(lo[k]) + 16, f"lowest {lo[k]:.2f} %", "ink", "middle")
    for h, lab in _day_ticks(LONG):
        g.text(sx(h), B + 18, lab, "faint", "middle")
    cfg = dict(W=960, x0=X0, x1=X1, top=T, bottom=B, xs=hs, hours=n, xlabels=[r["local_time"].replace("T", " ") for r in LONG],
               series=[dict(name="Hourly median", unit="%", dec=2, vals=med, vmin=0, vmax=6, ytop=T, ybot=B, color="--s1"),
                       dict(name="Hourly lowest", unit="%", dec=2, vals=lo, vmin=0, vmax=6, ytop=T, ybot=B, color="--s1")])
    return g.done(), cfg

def chart_settle(p, standalone):
    X0, X1, T, B, V0, V1 = 56, 880, 30, 230, 4.7, 5.2
    n = int(LONG[-1]["hour"])
    sx = lambda h: X0 + (X1 - X0) * h / n
    sy = lambda v: B - (B - T) * (v - V0) / (V1 - V0)
    g = Svg(960, 280, "The resting reading climbs for three days after installation and then stays level", p, standalone)
    g.text(X0, 18, "Resting CO₂, %", "ink", weight=600)
    for v in (4.7, 4.8, 4.9, 5.0, 5.1, 5.2):
        g.hline(X0, X1, sy(v)); g.text(X0 - 8, sy(v) + 4, f"{v:.1f}", "faint", "end")
    pts = [(sx(h), sy(v)) for h, v in _q]
    g.line(pts, "s1")
    g.dot(pts[0][0], pts[0][1], "s1"); g.text(pts[0][0] + 12, pts[0][1] + 4, f"{_q[0][1]:.2f} % at the start", "ink")
    g.dot(pts[-1][0], pts[-1][1], "s1"); g.text(X1 + 10, pts[-1][1] + 4, f"{_q[-1][1]:.2f} %", "ink")
    for h, lab in _day_ticks(LONG):
        g.text(sx(h), B + 18, lab, "faint", "middle")
    idx = {int(r["hour"]): r for r in LONG}
    cfg = dict(W=960, x0=X0, x1=X1, top=T, bottom=B, xs=[h for h, _ in _q], hours=n,
               xlabels=[idx[h]["local_time"].replace("T", " ") for h, _ in _q],
               series=[dict(name="Resting median", unit="%", dec=3, vals=[v for _, v in _q], vmin=V0, vmax=V1, ytop=T, ybot=B, color="--s1")])
    return g.done(), cfg

LONG_TILES = [
 ("Running without a restart", f"{LS['days']:.1f} days"),
 ("Time inside the band", f"{LS['in_band_pct']:.0f} %"),
 ("Left the band", f"{LS['excursions']} times"),
 ("Time above the band", "none"),
]
LONG_FINDINGS = [
 f"The resting reading climbed from {SETTLE_START:.2f} % to {SETTLE_END:.2f} % over the first three days, then held.",
 "Every excursion was a dip, never a rise.",
 f"The deepest dip reached {LS['lowest_pct']:.2f} %.",
]

def section_long():
    o = ['<section id="long-run"><h2>Ten days in the incubator</h2>'
         f'<p>One unbroken log from the SD card: {LS["samples"]:,} readings, summarised by the hour.</p><div class="tiles">']
    o += [f'<div class="tile"><small>{e(k)}</small><b>{e(v)}</b></div>' for k, v in LONG_TILES]
    o.append('</div>')
    svg, cfg = chart_days(CHART_LIGHT, False)
    o.append('<figure>' + chart_block("c-days", svg, cfg) +
             '<figcaption>The line is each hour’s median. The shading spans that hour’s lowest and highest reading.</figcaption></figure>')
    o.append('<ul class="find">' + "".join(f"<li>{e(f)}</li>" for f in LONG_FINDINGS) + '</ul>')
    svg, cfg = chart_settle(CHART_LIGHT, False)
    o.append('<figure><h3>The sensor needs three days to settle</h3>' + chart_block("c-settle", svg, cfg) +
             '<figcaption>Hourly median of readings taken while the door was shut. The axis starts at 4.7 %, not zero, to make the climb visible.</figcaption></figure>')
    o.append('</section>')
    return "\n".join(o)

def md_long():
    for name, fn in (("days", chart_days), ("settle", chart_settle)):
        open(os.path.join(DOCS, "img", name + ".svg"), "w").write(fn(CHART_DARK, True)[0] + "\n")
    o = ["## Ten days in the incubator", "",
         f"One unbroken log from the SD card: {LS['samples']:,} readings, summarised by the hour in "
         "[data/long-run-hourly.csv](data/long-run-hourly.csv).", "",
         "| " + " | ".join(k for k, _ in LONG_TILES) + " |", "|" + "---|" * len(LONG_TILES),
         "| " + " | ".join(v for _, v in LONG_TILES) + " |", "",
         "![Hourly CO₂ over ten days](img/days.svg)", ""]
    o += [f"- {f}" for f in LONG_FINDINGS]
    o += ["", "### The sensor needs three days to settle", "", "![Resting reading over ten days](img/settle.svg)", ""]
    return o

def build_html():
    o = []; a = o.append
    a("<title>CO₂ Monitor Build Guide</title>")
    a('<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap">')
    a(f"<style>{CSS}</style>")
    a('<div class="wrap">')
    a(f'<header><div class="eyebrow">TGGR Lab · firmware {VERSION}</div>'
      '<h1>DIY cell culture CO₂ monitor</h1>'
      '<p class="lede">We built our own CO₂ monitor for our cell culture incubator. It was fun, and you can try too.</p></header>')
    a('<section><div class="hero">'
      f'<figure class="photo"><img src="{photo_uri("reading")}" width="1600" height="1087" alt="The main page reading 4.95 percent with status NORMAL">'
      f'<figcaption>{e(HERO_CAPTIONS[0])}</figcaption></figure>'
      f'<figure class="photo"><img src="{photo_uri("installed")}" width="1600" height="1087" alt="The monitor in place, reading 6.66 percent with status HIGH CO2">'
      f'<figcaption>{e(HERO_CAPTIONS[1])}</figcaption></figure></div>')
    a('<div class="points">')
    for t, b in INTRO: a(f'<div><h3>{e(t)}</h3><p>{e(b)}</p></div>')
    a('</div>'
      f'<figure class="photo" style="max-width:560px"><img src="{photo_uri("breadboard")}" alt="A bare ESP32-S3 on a breadboard wired to the MH-Z16">'
      '<figcaption>The first test ran on a bare ESP32-S3 with no screen.</figcaption></figure></section>')

    a('<section><h2>Parts</h2><div class="scroll"><table><thead><tr><th>Part</th><th>Qty</th><th>Notes</th><th>Find it</th></tr></thead><tbody>')
    for part, qty, note, q in PARTS:
        link = f'<a href="{search_url(q)}" target="_blank" rel="noopener">Search AliExpress</a>' if q else ""
        a(f'<tr><td>{e(part)}</td><td class="n">{e(qty)}</td><td>{e(note)}</td><td class="n">{link}</td></tr>')
    a('</tbody></table></div></section>')

    a('<section><h2>Wiring map</h2>')
    a('<figure><div class="map">' + wiring_svg(CSS_PALETTE, False) + '</div>'
      '<figcaption>Dots show direction: power runs out to the sensors, signals run back to the board.</figcaption></figure>')
    a(f'<figure class="photo" style="max-width:620px"><img src="{photo_uri("board-back")}" alt="The back of the board with its three sockets">'
      '<figcaption>Pin names are printed beside each socket.</figcaption></figure>')
    a('<div class="scroll"><table><thead><tr><th>Wire</th><th>Socket</th><th>Pin</th></tr></thead><tbody>')
    for w, c, pin in PINS:
        a(f'<tr><td>{e(w)}</td><td class="n">{e(c)}</td><td><span class="pin">{e(pin)}</span></td></tr>')
    a('</tbody></table></div></section>')

    a('<section><h2>Build order</h2><ol class="steps">')
    for title, body, cmd, photos in STEPS:
        a(f'<li><div><h3>{e(title)}</h3><p>{e(body)}</p>')
        if cmd: a(f'<pre class="cmd">{e(cmd)}</pre>')
        if photos:
            a('<div class="pair">')
            for name, cap in photos:
                a(f'<figure class="photo"><img src="{photo_uri(name)}" alt="{e(cap)}"><figcaption>{e(cap)}</figcaption></figure>')
            a('</div>')
        a('</div></li>')
    a('</ol></section>')

    a('<section><h2>Reading the screens</h2><p>Tap anywhere to move to the next page. There are four: main, graph, diagnostics and connect.</p><div class="screens">')
    for name, title, marks in SCREENS:
        b64 = base64.b64encode(open(annotate(name, marks), "rb").read()).decode()
        a(f'<div class="screen"><h3>{e(title)}</h3>'
          f'<img src="data:image/png;base64,{b64}" width="768" height="512" alt="{e(title)} with numbered callouts"><ol>')
        for _, _, _, label in marks: a(f'<li><span>{e(label)}</span></li>')
        a('</ol></div>')
    a(f'<figure class="photo" style="max-width:620px"><img src="{photo_uri("door-events")}" alt="Main page with two orange door markers above the plot">'
      '<figcaption>Two door openings, flagged in orange. CO₂ falls each time and climbs back.</figcaption></figure>')
    a('</div></section>')

    a('<section><h2>How it measures</h2>'
      '<p>The sensor sends one pulse a second. The longer the pulse, the more CO₂.</p>'
      f'<figure><div class="map">{PWM_SVG}</div>'
      '<figcaption>Room air gives a pulse of a few milliseconds. At 5 % the pulse fills half the cycle.</figcaption></figure>'
      + flows_html() + '</section>')
    a(section_hour())
    a(section_long())
    a(section_dashboard())

    a('<section id="calibration"><h2>Calibrate before trusting it</h2><ol class="cal">')
    for c in CAL_STEPS: a(f'<li>{e(c)}</li>')
    a(f'</ol><p class="note">{e(CAL_NOTE)}</p></section>')

    a('<section><h2>Checks and faults</h2>'
      '<div class="scroll"><table><thead><tr><th>Diagnostics row</th><th>Healthy</th><th>If not</th></tr></thead><tbody>')
    for row, ok, bad in CHECKS:
        a(f'<tr><td><span class="pin">{e(row)}</span></td><td>{e(ok)}</td><td>{e(bad)}</td></tr>')
    a('</tbody></table></div><dl>')
    for q, ans in TROUBLE: a(f'<div><dt>{e(q)}</dt><dd>{e(ans)}</dd></div>')
    a('</dl></section>')
    a(f'<footer class="note">TGGR Lab. {e(LICENCE_TEXT)} '
      f'Source and firmware: <a href="{REPO_URL}" target="_blank" rel="noopener">{e(REPO_URL.split("//")[1])}</a></footer>')
    a('</div>')
    a(PWM_JS)
    a(CHART_JS)
    return "\n".join(o).replace('class="flow-box"', 'class="scroll" style="padding:14px"')

def build_md():
    o = ["# DIY cell culture CO₂ monitor", "",
         "We built our own CO₂ monitor for our cell culture incubator. It was fun, and you can try too. "
         f"Firmware {VERSION}.", "",
         f"![{HERO_CAPTIONS[0]}](photos/reading.jpg)", "",
         HERO_CAPTIONS[0], "",
         f"![{HERO_CAPTIONS[1]}](photos/installed.jpg)", "",
         HERO_CAPTIONS[1], ""]
    for t, b in INTRO: o += [f"**{t}.** {b}", ""]
    o += ["![The first test ran on a bare ESP32-S3 with no screen](photos/breadboard.jpg)", ""]
    o += ["## Parts", "", "| Part | Qty | Notes | Find it |", "|---|---|---|---|"]
    o += [f"| {p} | {q} | {n} | {'[Search AliExpress](' + search_url(sq) + ')' if sq else ''} |" for p, q, n, sq in PARTS]
    o += ["", "## Wiring map", "", "![Wiring map](wiring-map.svg)", "",
          "![Pin names are printed beside each socket](photos/board-back.jpg)", "",
          "| Wire | Socket | Pin |", "|---|---|---|"]
    o += [f"| {w} | {c} | `{pin}` |" for w, c, pin in PINS]
    o += ["", "## Build order", ""]
    for i, (t, b, cmd, photos) in enumerate(STEPS, 1):
        o.append(f"{i}. **{t}.** {b}")
        if cmd: o += ["", "   ```bash"] + ["   " + l for l in cmd.split("\n")] + ["   ```"]
        for name, cap in photos: o += ["", f"   ![{cap}](photos/{name}.jpg)"]
        o.append("")
    o += ["## Reading the screens", "", "Tap anywhere to move to the next page. There are four: main, graph, diagnostics and connect.", ""]
    for name, title, marks in SCREENS:
        annotate(name, marks)
        o += [f"### {title}", "", f"![{title}](img/{name}.png)", ""]
        o += [f"{i}. {label}" for i, (_, _, _, label) in enumerate(marks, 1)] + [""]
    o += ["![Two door openings flagged in orange](photos/door-events.jpg)", "",
          "## How it measures", "",
          "The sensor sends one pulse a second. The longer the pulse, the more CO₂.", "",
          "    ppm = 100000 × (high − 2 ms) / (cycle − 4 ms)", "",
          *md_flows(),
          *md_hour(), *md_long(), *md_dashboard(),
          "## Calibrate before trusting it", ""]
    o += [f"{i}. {c}" for i, c in enumerate(CAL_STEPS, 1)]
    o += ["", CAL_NOTE, "", "## Checks and faults", "", "| Diagnostics row | Healthy | If not |", "|---|---|---|"]
    o += [f"| `{r}` | {ok} | {bad} |" for r, ok, bad in CHECKS] + [""]
    for q, ans in TROUBLE: o += [f"**{q}.** {ans}", ""]
    o += ["---", f"TGGR Lab. {LICENCE_TEXT}", "",
          "Generated by `tools/make_build_guide.py`. Edit that script, not this file."]
    return "\n".join(o) + "\n"

if __name__ == "__main__":
    os.makedirs(DOCS, exist_ok=True)
    open(os.path.join(DOCS, "wiring-map.svg"), "w").write(wiring_svg(DARK_PALETTE, True) + "\n")
    open(os.path.join(DOCS, "index.html"), "w").write(build_html() + "\n")
    open(os.path.join(DOCS, "BUILD_GUIDE.md"), "w").write(build_md())
    for stale in ("boot", "diagnostics"):
        p = os.path.join(DOCS, "img", stale + ".png")
        if os.path.exists(p): os.remove(p)
    print("wrote docs/wiring-map.svg, docs/index.html, docs/BUILD_GUIDE.md")
