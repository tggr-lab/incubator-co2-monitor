#!/usr/bin/env python3
"""
Grab the CYD's framebuffer over serial and save it as a PNG.

    tools/screenshot.py                     # current screen -> shot.png
    tools/screenshot.py -p 2 -o graph.png   # switch to page 2 first
    tools/screenshot.py -b -o boot.png      # replay the boot splash, wait, grab

Talks to the debug commands in CO2_CYD.ino. Opens the port without pulsing
DTR/RTS so the board is not reset by the act of connecting.
"""
import argparse, struct, sys, time
import serial
from PIL import Image

def open_quiet(port, baud):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, baud, 2
    s.dtr = False; s.rts = False          # applied at open -> no reset pulse
    s.open()
    return s

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-P", "--port", default="/dev/ttyUSB0")
    ap.add_argument("-o", "--out", default="shot.png")
    ap.add_argument("-p", "--page", choices=["1","2","3","4"])
    ap.add_argument("-b", "--boot", action="store_true", help="replay splash first")
    ap.add_argument("--delay", type=float, default=0.8, help="seconds to settle before grabbing")
    ap.add_argument("--little", action="store_true",
                    help="decode pixels little-endian (the panel readback is big-endian)")
    ap.add_argument("--settle", type=float, default=0.0,
                    help="seconds to wait after opening the port. Opening resets the "
                         "board on this CH340, so use ~6 to get past the boot splash")
    ap.add_argument("--all", metavar="PREFIX",
                    help="capture pages 1-4 and the boot splash in one session "
                         "as PREFIX_main/graph/diag/connect/boot.png")
    a = ap.parse_args()

    s = open_quiet(a.port, 115200)
    time.sleep(0.2)
    if a.settle: time.sleep(a.settle)
    s.reset_input_buffer()

    if a.all:
        plan = [("1", a.delay, f"{a.all}_main.png"), ("2", a.delay, f"{a.all}_graph.png"),
                ("3", a.delay, f"{a.all}_diag.png"), ("4", a.delay, f"{a.all}_connect.png"),
                ("b", 1.6, f"{a.all}_boot.png")]
        for cmd, wait, out in plan:
            s.write(cmd.encode()); time.sleep(wait)
            s.reset_input_buffer()
            grab(s, out, a)
        return

    if a.boot:
        s.write(b"b"); time.sleep(1.6)
    if a.page:
        s.write(a.page.encode()); time.sleep(a.delay)
    grab(s, a.out, a)

def grab(s, out_path, a):
    s.write(b"S")

    # find header
    deadline = time.time() + 6
    hdr = b""
    while time.time() < deadline:
        line = s.readline()
        if line.startswith(b"SCREENSHOT"):
            hdr = line; break
    if not hdr:
        sys.exit("no SCREENSHOT header received (is the debug build flashed?)")
    _, w, h = hdr.split(); w, h = int(w), int(h)

    need = w * h * 2
    buf = bytearray()
    t0 = time.time()
    s.timeout = 5
    while len(buf) < need and time.time() - t0 < 60:
        chunk = s.read(min(8192, need - len(buf)))
        if not chunk: break
        buf += chunk
    if len(buf) < need:
        sys.exit(f"short read: {len(buf)}/{need} bytes")

    px = struct.unpack(f"{'<' if a.little else '>'}{w*h}H", bytes(buf))
    img = Image.new("RGB", (w, h))
    out = img.load()
    for i, v in enumerate(px):
        r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
        out[i % w, i // w] = ((r * 255) // 31, (g * 255) // 63, (b * 255) // 31)
    img.save(out_path)
    print(f"saved {out_path} ({w}x{h}) in {time.time()-t0:.1f}s", flush=True)

if __name__ == "__main__":
    main()
