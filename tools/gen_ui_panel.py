#!/usr/bin/env python3
"""Generate examples/assets/textures/ui_panel.png: the nine-slice panel the `ui`
example draws its frames with. 48x48 RGBA, a rounded translucent panel with a
lighter border, meant to be sliced 16 px in from every side, so the corners keep
their radius at any size. Needs nothing but the standard library. Run from anywhere:

    tools/gen_ui_panel.py
"""
import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, "examples/assets/textures/ui_panel.png")

SIZE = 48
RADIUS = 14.0
BORDER = 2.5  # border thickness, in pixels, inside the rounded edge
FILL = (26, 30, 42, 235)
EDGE = (120, 145, 200, 255)


def coverage(x, y, inset):
    """How much of pixel (x, y) is inside the rounded rectangle shrunk by `inset`
    (4x4 supersampled, so the curves don't stair-step)."""
    hits = 0
    for sy in range(4):
        for sx in range(4):
            px, py = x + (sx + 0.5) / 4.0, y + (sy + 0.5) / 4.0
            # distance outside the rounded rectangle, by mirroring into one corner
            dx = max(inset + RADIUS - px, px - (SIZE - inset - RADIUS), 0.0)
            dy = max(inset + RADIUS - py, py - (SIZE - inset - RADIUS), 0.0)
            if math.hypot(dx, dy) <= RADIUS - inset:
                hits += 1
    return hits / 16.0


def main():
    rows = []
    for y in range(SIZE):
        row = bytearray([0])  # PNG filter type 0 (none)
        for x in range(SIZE):
            outer = coverage(x, y, 0.0)
            inner = coverage(x, y, BORDER)
            edge = max(outer - inner, 0.0)
            alpha = FILL[3] * inner + EDGE[3] * edge
            if alpha <= 0.0:
                row += bytes(4)
                continue
            for c in range(3):
                weight = (FILL[c] * FILL[3] * inner + EDGE[c] * EDGE[3] * edge) / alpha
                row.append(int(round(weight)))
            row.append(int(round(alpha)))
        rows.append(bytes(row))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) +
           chunk(b"IEND", b""))
    with open(OUTPUT, "wb") as f:
        f.write(png)
    print("wrote %s (%d bytes)" % (os.path.relpath(OUTPUT, ROOT), len(png)))


if __name__ == "__main__":
    main()
