#!/usr/bin/env python3
# Marine VHF read-only assets: the channel digits, packed from digits.txt (big)
# and digits_small.txt (the 10/20 in front of a four-digit channel).
# SPDX-License-Identifier: Apache-2.0 (the digit bitmaps: see digits.txt and NOTICE)
#
# Layout (read by marine_app.c with asset_read):
#   10 bytes      width of each big digit 0-9, in columns
#   10 bytes      width of each small digit 0-9
#   then each big digit as 6 LCD pages (48 rows) of `width` column bytes, page
#   after page, then each small digit as 3 pages (24 rows). Bit 0 of a byte is
#   the top row of its page, as in the frame buffer, so a glyph is ORed (or,
#   inverse, cleared) straight into it: big ones on lines 0-5, small on 3-5.
#
#   ./gen_assets.py marine_assets.bin
import os, sys

MAXW = 32                      # GLYPH_MAXW in marine_app.c (its stack buffer)


def load(path, rows):
    glyphs, cur = {}, None
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith("# "):      # a comment (glyph rows have no space)
            continue
        if len(line) == 1 and line.isdigit():
            cur = glyphs.setdefault(int(line), [])
            continue
        cur.append(line)
    assert sorted(glyphs) == list(range(10)), f"{path}: 0-9 expected"
    out = []
    for d in range(10):
        g = glyphs[d]
        assert len(g) == rows and len({len(r) for r in g}) == 1, f"{path}, {d}: {rows} rows of one width"
        cols = [c for c in range(len(g[0])) if any(r[c] == "#" for r in g)]
        lo, hi = cols[0], cols[-1] + 1            # trim blank side columns: the app spaces glyphs
        g = [r[lo:hi] for r in g]
        assert len(g[0]) <= MAXW, f"{path}, {d}: {len(g[0])} columns, {MAXW} at most"
        out.append(g)
    return out


def pages(g):
    out = bytearray()
    for p in range(len(g) // 8):
        for c in range(len(g[0])):
            out.append(sum(1 << b for b in range(8) if g[p * 8 + b][c] == "#"))
    return out


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    big = load(os.path.join(here, "digits.txt"), 48)
    small = load(os.path.join(here, "digits_small.txt"), 24)
    blob = bytearray(len(g[0]) for g in big) + bytearray(len(g[0]) for g in small)
    for g in big + small:
        blob += pages(g)
    open(sys.argv[1], "wb").write(bytes(blob))
