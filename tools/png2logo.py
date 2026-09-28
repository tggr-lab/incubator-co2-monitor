#!/usr/bin/env python3
"""
Convert a PNG into the RGB565 C array the firmware draws as the lab logo.

    tools/png2logo.py mylab.png -o firmware/CO2_CYD/Logo.h

The firmware never knows anything about the artwork beyond its width, height
and pixel data, so replacing the logo is exactly this one command followed by a
rebuild. Nothing else in the source refers to the image.

Transparency is flattened against the UI background rather than carried into
the firmware: the panel has no alpha channel, and compositing at runtime would
cost RAM and time for no visible gain. One colour is reserved as "transparent"
so the logo can still sit on a non-background surface if that is ever wanted.
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install --user Pillow")

# Must match theme::BG in Theme.h. Artwork is flattened onto this.
DEFAULT_BG = "#000000"

# Any pixel that lands exactly on this RGB565 value is skipped when drawing.
# Chosen as a colour no real logo would contain.
TRANSPARENT_565 = 0xF81F  # pure magenta


def to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", help="source image; any format Pillow can open")
    ap.add_argument("-o", "--out", default="Logo.h", help="header to write")
    ap.add_argument("-W", "--width", type=int, default=28,
                    help="target width in pixels (default 28, the header slot)")
    ap.add_argument("-H", "--height", type=int, default=28,
                    help="target height in pixels (default 28)")
    ap.add_argument("--bg", default=DEFAULT_BG,
                    help=f"colour to flatten transparency onto (default {DEFAULT_BG})")
    ap.add_argument("--keep-alpha", action="store_true",
                    help="emit fully transparent pixels as the skip colour "
                         "instead of flattening them onto --bg")
    ap.add_argument("--name", default="LOGO", help="C identifier prefix")
    ap.add_argument("--crop", default=None,
                    help="source box to use before resizing: 'auto' to trim to "
                         "the artwork's bounding box, or 'left,top,right,bottom' "
                         "in source pixels")
    ap.add_argument("--dark", action="store_true",
                    help="dark-mode conversion: invert the luminance of neutral "
                         "(low-saturation) pixels so dark ink reads light on a "
                         "dark ground and the white card becomes background; "
                         "brand colours keep their hue and are lifted slightly")
    ap.add_argument("--circle", action="store_true",
                    help="mask everything outside the inscribed circle to the "
                         "skip colour. Use for a round logo so its corners do "
                         "not paint a square patch of --bg on the screen")
    args = ap.parse_args()

    bg = tuple(int(args.bg.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))

    src = Image.open(args.png).convert("RGBA")

    if args.crop == "auto":
        # Trim to the artwork itself. Transparent *and* white-ish pixels count
        # as background, since logos are usually supplied on a white card.
        flat = Image.new("RGB", src.size, (255, 255, 255))
        flat.paste(src, mask=src.getchannel("A"))
        ink = flat.convert("L").point(lambda p: 255 if p < 240 else 0)
        box = ink.getbbox()
        if box:
            src = src.crop(box)
    elif args.crop:
        src = src.crop(tuple(int(v) for v in args.crop.split(",")))
    # LANCZOS keeps thin strokes readable at logo sizes; NEAREST would alias
    # a fine wordmark into mush.
    src = src.resize((args.width, args.height), Image.LANCZOS)

    # A round logo rendered into a square array paints its corners with the
    # flattened background, which shows as a box on a dark UI. Masking them to
    # the skip colour lets the firmware leave those pixels alone.
    if args.circle:
        from PIL import ImageDraw
        mask = Image.new("L", (args.width, args.height), 0)
        ImageDraw.Draw(mask).ellipse([0, 0, args.width - 1, args.height - 1], fill=255)
    else:
        mask = None

    if args.dark:
        import colorsys
        px = src.load()
        for y in range(src.size[1]):
            for x in range(src.size[0]):
                r, g, b, a = px[x, y]
                h, sat, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                if sat < 0.28 or v < 0.33:
                    # Ink or card: flip it. "Ink" includes dark pixels of any
                    # hue -- this logo's outlines are a near-black teal, and
                    # they are what make the tiger legible at 26 px. A trace of
                    # the hue is kept so the flipped strokes are not clinical.
                    # Flip, then lift with a gamma: a one-pixel stroke that the
                    # resampler rendered as pale grey on white must come out as
                    # readable grey on black, not 5 % brightness. Solid ink
                    # (v near 0) still lands at near white.
                    v = (1.0 - v) ** 0.45
                    sat = sat * 0.35
                else:
                    # Brand colour: keep the hue, lift the value for the dark
                    # ground, and the very dark navy-teal shadows come up too.
                    v = min(1.0, v * 1.35 + 0.08)
                r2, g2, b2 = colorsys.hsv_to_rgb(h, sat, v)
                px[x, y] = (round(r2 * 255), round(g2 * 255), round(b2 * 255), a)

    words, skipped = [], 0
    for y in range(args.height):
        for x in range(args.width):
            r, g, b, a = src.getpixel((x, y))
            if mask is not None and mask.getpixel((x, y)) < 128:
                words.append(TRANSPARENT_565)
                skipped += 1
                continue
            if args.keep_alpha and a < 8:
                words.append(TRANSPARENT_565)
                skipped += 1
                continue
            # Straight alpha composite onto the flat UI background.
            f = a / 255.0
            r = round(r * f + bg[0] * (1 - f))
            g = round(g * f + bg[1] * (1 - f))
            b = round(b * f + bg[2] * (1 - f))
            words.append(to_565(r, g, b))

    n = args.name
    lines = [
        "#pragma once",
        "//",
        f"// {n} artwork -- GENERATED FILE, do not hand-edit.",
        "//",
        f"//   regenerate:  tools/png2logo.py <your.png> -o {args.out}",
        "//",
        f"// Source image : {args.png}",
        f"// Size         : {args.width}x{args.height}, RGB565, row-major",
        f"// Flattened on : {args.bg}",
        "//",
        "// Pixels equal to LOGO_TRANSPARENT are skipped by the drawing code, so a",
        "// logo with a cut-out can sit on top of a coloured surface.",
        "//",
        "#include <Arduino.h>",
        "",
        f"constexpr int16_t  {n}_WIDTH  = {args.width};",
        f"constexpr int16_t  {n}_HEIGHT = {args.height};",
        f"constexpr uint16_t {n}_TRANSPARENT = 0x{TRANSPARENT_565:04X};",
        "",
        f"// {args.width * args.height} pixels, {args.width * args.height * 2} bytes, kept in flash.",
        f"const uint16_t {n}_DATA[{args.width * args.height}] PROGMEM = {{",
    ]
    for i in range(0, len(words), 12):
        chunk = ", ".join(f"0x{w:04X}" for w in words[i:i + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")

    with open(args.out, "w") as fh:
        fh.write("\n".join(lines))

    print(f"wrote {args.out}: {args.width}x{args.height}, "
          f"{len(words) * 2} bytes, {skipped} transparent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
