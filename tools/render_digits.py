#!/usr/bin/env python3
"""Render the big channel digits from a font into app/marine/digits.txt.

The app draws the channel number from a 1-bit bitmap of the ten digits, kept in
its read-only assets (app/marine/gen_assets.py packs digits.txt). This script
makes digits.txt from an outline font; the text file is then the source, and
can be retouched by hand pixel by pixel.

Each glyph is 48 rows (6 LCD pages), the digits' ink on rows TOP..TOP+HEIGHT-1,
on a common baseline, cropped to its own ink horizontally (the app spaces them).

    pip install pillow
    python3 tools/render_digits.py RobotoCondensed.ttf --weight 700 > app/marine/digits.txt
    python3 tools/render_digits.py RobotoCondensed.ttf --weight 700 --small \
        > app/marine/digits_small.txt      # the 10/20 of 1019, 2078...
    python3 tools/render_digits.py FONT --preview out.png      # a look at 16, 72, 1019...

--small: the prefix digits of the four-digit channels, 22 rows high on 24 (3 LCD
pages, the app's frame-buffer lines 3-5), ending on the big digits' baseline.
"""
import argparse, sys
from PIL import Image, ImageDraw, ImageFont

ROWS, TOP, HEIGHT = 48, 2, 44   # cell height, first ink row, ink height
SMALL = (24, 0, 22)             # the same for --small: rows 24-45 of the screen
MAXW = 29                       # widest glyph: 4 digits + 3 gaps of GAP fit in 128 px
GAP = 3                         # as DIG_GAP in marine_app.c (for the preview)


def font(path, size, weight):
    f = ImageFont.truetype(path, size)
    if weight:
        try:
            f.set_variation_by_axes([weight])
        except OSError:
            pass                      # not a variable font
    return f


def ink(f, d):
    """The glyph drawn at a fixed origin, and its ink box."""
    size = f.size
    im = Image.new("L", (size * 2, size * 2), 0)
    ImageDraw.Draw(im).text((size // 2, size // 2), d, font=f, fill=255)
    return im, im.getbbox()


def render(path, weight, threshold, xscale):
    # the size at which the digits' common ink height (overshoots included) is HEIGHT
    size = 60
    for _ in range(8):
        f = font(path, size, weight)
        boxes = [ink(f, d)[1] for d in "0123456789"]
        h = max(b[3] for b in boxes) - min(b[1] for b in boxes)
        if h == HEIGHT:
            break
        size = max(8, round(size * HEIGHT / h)) if abs(h - HEIGHT) > 1 else size + (1 if h < HEIGHT else -1)
    f = font(path, size, weight)
    rendered = [ink(f, d) for d in "0123456789"]
    top = min(b[1] for _, b in rendered)
    widest = max(b[2] - b[0] for _, b in rendered)
    sx = min(xscale, MAXW / widest)
    glyphs = []
    for im, b in rendered:
        g = im.crop((b[0], top, b[2], top + HEIGHT))
        w = max(1, round((b[2] - b[0]) * sx))
        if w != g.size[0]:
            g = g.resize((w, HEIGHT), Image.LANCZOS)
        cell = Image.new("L", (w, ROWS), 0)
        cell.paste(g, (0, TOP))
        glyphs.append([[cell.getpixel((x, y)) >= threshold for x in range(w)] for y in range(ROWS)])
    return glyphs, size, sx


def text(glyphs, source):
    out = [f"# Marine VHF {'small' if ROWS == SMALL[0] else 'big'} digits: {source}",
           f"# 10 glyphs of {ROWS} rows, '#' = pixel on. Packed by gen_assets.py; may be retouched by hand."]
    for d, g in enumerate(glyphs):
        out.append(str(d))
        out += ["".join("#" if p else "." for p in row) for row in g]
    return "\n".join(out) + "\n"


def preview(glyphs, path, scale=4):
    texts = ["16", "72", "05", "1019", "2078", "88", "34"]
    W = 128 * scale + 8
    out = Image.new("RGB", (len(texts) * W, ROWS * scale), (40, 40, 40))
    for i, t in enumerate(texts):
        im = Image.new("RGB", (128, ROWS), (205, 210, 220))
        ds = [glyphs[int(c)] for c in t]
        w = sum(len(g[0]) for g in ds) + GAP * (len(ds) - 1)
        x = (128 - w) // 2
        for g in ds:
            for y, row in enumerate(g):
                for c, p in enumerate(row):
                    if p:
                        im.putpixel((x + c, y), (35, 38, 70))
            x += len(g[0]) + GAP
        out.paste(im.resize((128 * scale, ROWS * scale), Image.NEAREST), (i * W, 0))
    out.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("font")
    ap.add_argument("--weight", type=int, default=0, help="wght axis of a variable font")
    ap.add_argument("--threshold", type=int, default=128, help="antialiased level that becomes a pixel")
    ap.add_argument("--xscale", type=float, default=1.0, help="horizontal scale (narrower < 1)")
    ap.add_argument("--source", default=None, help="font credit written in the header")
    ap.add_argument("--preview", metavar="PNG")
    ap.add_argument("--small", action="store_true", help="the four-digit channels' prefix digits")
    a = ap.parse_args()
    if a.small:
        global ROWS, TOP, HEIGHT
        ROWS, TOP, HEIGHT = SMALL
    glyphs, size, sx = render(a.font, a.weight, a.threshold, a.xscale)
    if a.preview:
        preview(glyphs, a.preview)
        print(f"{a.font}: {size} px, x{sx:.2f}, widths {[len(g[0]) for g in glyphs]}", file=sys.stderr)
    else:
        sys.stdout.write(text(glyphs, a.source or a.font))


if __name__ == "__main__":
    main()
