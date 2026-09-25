#!/usr/bin/env python3
"""Generate examples/assets/textures/tiles.png, the sprite sheet the `tilemap` example
draws its world from, and tiles_sheet_normal.png, its tangent-space normal map (the
`lights` example's lit sprites).

Every cell has a 2 px gutter on each side that repeats its edge pixels, so a sampler
reaching past the cell (bilinear filtering, a mipmap, an MSAA edge pixel) finds more
of the cell and not its neighbour. The sheet is 80x56 RGBA; the cells, in pixels
(x, y, width, height), are

    grass (2, 2, 16, 16)    sand (22, 2, 16, 16)   water (42, 2, 16, 16)   stone (62, 2, 16, 16)
    tree (2, 22, 16, 32)    flag (22, 22, 16, 32)  coin (42, 22, 16, 16)   rock (62, 22, 16, 16)

Terrain is opaque, the props are on a transparent background. Pixel art on purpose:
the tilemap example samples it with nearest filtering. The normal map is the same
layout at 4x the resolution, from a height field the same shapes draw, so a cell cut
from the sheet cuts the matching relief. Needs nothing but the standard library. Run
from anywhere:

    tools/gen_tiles.py
"""
import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, "examples/assets/textures/tiles.png")
NORMAL_OUTPUT = os.path.join(ROOT, "examples/assets/textures/tiles_sheet_normal.png")

# The shapes draw on a gutterless 64x48 canvas; the sheet is the canvas's cells
# spread apart by the gutters.
CANVAS_W, CANVAS_H = 64, 48
GUTTER = 2
SCALE = 4  # height field (and normal map) texels per canvas pixel
px = [[(0, 0, 0, 0)] * CANVAS_W for _ in range(CANVAS_H)]
hf = [[0.0] * (CANVAS_W * SCALE) for _ in range(CANVAS_H * SCALE)]  # in canvas pixels

# The cells, on the canvas: name, x, y, width, height. A normal is taken within its
# cell, and a gutter repeats its cell's edge, so no relief reaches a neighbour.
CELLS = [("grass", 0, 0, 16, 16), ("sand", 16, 0, 16, 16), ("water", 32, 0, 16, 16), ("stone", 48, 0, 16, 16),
         ("tree", 0, 16, 16, 32), ("flag", 16, 16, 16, 32), ("coin", 32, 16, 16, 16), ("rock", 48, 16, 16, 16)]


def sheet_xy(cx, cy):
    """Where a cell's origin lands in the sheet: cells are on a 16 px grid on the
    canvas, and each column or row before it adds two gutters."""
    return cx + GUTTER * (2 * (cx // 16) + 1), cy + GUTTER * (2 * (cy // 16) + 1)


WIDTH = max(sheet_xy(cx, 0)[0] + cw + GUTTER for _, cx, cy, cw, ch in CELLS)  # 80
HEIGHT = max(sheet_xy(0, cy)[1] + ch + GUTTER for _, cx, cy, cw, ch in CELLS)  # 56


def put(x, y, color, height=None):
    """One canvas pixel; `height`, when given, sets the relief under it."""
    if 0 <= x < CANVAS_W and 0 <= y < CANVAS_H and color[3] > 0:
        px[y][x] = color
        if height is not None:
            for j in range(SCALE):
                for i in range(SCALE):
                    hf[y * SCALE + j][x * SCALE + i] = height


def rect(x, y, w, h, color, height=None, bulge=0.0):
    """`bulge` rounds the relief across the width, like a pole or a trunk."""
    for j in range(h):
        for i in range(w):
            put(x + i, y + j, color)
    if height is None:
        return
    for j in range(y * SCALE, (y + h) * SCALE):
        for i in range(x * SCALE, (x + w) * SCALE):
            t = ((i + 0.5) / SCALE - x) / w * 2.0 - 1.0  # -1..1 across the width
            hf[j][i] = height + bulge * math.sqrt(max(0.0, 1.0 - t * t))


def disc(cx, cy, r, color, height=None, dome=0.0):
    """`dome` raises the middle above `height`, like a hemisphere."""
    for j in range(int(cy - r), int(cy + r) + 1):
        for i in range(int(cx - r), int(cx + r) + 1):
            if (i + 0.5 - cx) ** 2 + (j + 0.5 - cy) ** 2 <= r * r:
                put(i, j, color)
    if height is None:
        return
    for j in range(int((cy - r) * SCALE), int((cy + r) * SCALE) + 1):
        for i in range(int((cx - r) * SCALE), int((cx + r) * SCALE) + 1):
            d2 = ((i + 0.5) / SCALE - cx) ** 2 + ((j + 0.5) / SCALE - cy) ** 2
            if d2 <= r * r and 0 <= j < CANVAS_H * SCALE and 0 <= i < CANVAS_W * SCALE:
                hf[j][i] = height + dome * math.sqrt(1.0 - d2 / (r * r))


def noise(x, y, w, h, color, step, offset=0, height=None):
    """A sparse, repeatable speckle so flat tiles don't look like plain fills.
    A hash, not a linear pattern: `i * 7 + j * 13` draws visible diagonal stripes."""
    for j in range(h):
        for i in range(w):
            hashed = ((i + offset) * 73856093) ^ ((j + offset) * 19349663)
            if (hashed >> 4) % step == 0:
                put(x + i, y + j, color, height)


# --- terrain (16x16, opaque) ---
rect(0, 0, 16, 16, (86, 138, 74, 255), 0.0)          # grass
noise(0, 0, 16, 16, (104, 158, 88, 255), 5, height=0.5)
noise(0, 0, 16, 16, (72, 118, 64, 255), 7, 3, height=-0.5)

rect(16, 0, 16, 16, (204, 184, 130, 255), 0.0)       # sand
noise(16, 0, 16, 16, (222, 204, 154, 255), 5, height=0.3)
noise(16, 0, 16, 16, (186, 164, 112, 255), 9, 4, height=-0.3)

rect(32, 0, 16, 16, (58, 104, 160, 255), 0.0)        # water
for j in range(0, 16, 4):                            # wave lines
    for i in range(16):
        if (i + j) % 8 < 4:
            put(32 + i, j + 1, (86, 138, 194, 255), 0.5)

rect(48, 0, 16, 16, (128, 126, 124, 255), 0.0)       # stone
noise(48, 0, 16, 16, (150, 148, 146, 255), 4, height=0.4)
for i in range(16):                                  # a crack
    put(48 + i, (i * 5 // 8 + 3) % 16, (98, 96, 94, 255), -1.0)

# --- tall objects (16x32, transparent background) ---
rect(6, 16 + 18, 4, 14, (96, 68, 44, 255), 0.0, bulge=1.5)  # tree trunk
disc(8, 16 + 14, 7.5, (54, 106, 58, 255), 0.0, dome=4.0)    # canopy
disc(6, 16 + 12, 4.5, (72, 130, 70, 255))                   # lit side

rect(23, 16 + 2, 2, 30, (168, 168, 172, 255), 0.0, bulge=1.0)  # flag pole
for j in range(7):                                  # pennant (stays inside the 16 px cell)
    rect(25, 16 + 3 + j, 7 - j, 1, (196, 74, 66, 255), 0.3)

# --- small objects (16x16, transparent background) ---
disc(40, 16 + 8, 6.0, (214, 172, 54, 255), 1.5)     # coin: a raised rim
disc(40, 16 + 8, 3.5, (240, 212, 108, 255), 0.8, dome=0.4)
disc(56, 16 + 9, 5.5, (124, 120, 116, 255), 0.0, dome=3.5)  # rock
disc(54, 16 + 8, 3.0, (150, 146, 142, 255))


def normal_at(i, j, x0, y0, x1, y1):
    """The height field's normal at texel (i, j), looking no further than the cell."""
    l, r = max(i - 1, x0), min(i + 1, x1)
    u, d = max(j - 1, y0), min(j + 1, y1)
    # slopes in height per canvas pixel
    dx = (hf[j][r] - hf[j][l]) * SCALE / max(r - l, 1)
    dy = (hf[d][i] - hf[u][i]) * SCALE / max(d - u, 1)
    # glTF normal maps: +x right, +y up (the sheet's y runs down, so +dy)
    n = (-dx, dy, 1.0)
    length = math.sqrt(sum(c * c for c in n))
    return bytes(int(round((c / length * 0.5 + 0.5) * 255)) for c in n)


def compose(scale, blank, texel):
    """The sheet at `scale` texels per pixel: each cell with its gutter, from
    `texel(canvas i, canvas j, cell bounds)`, `blank` between them."""
    w, h, g = WIDTH * scale, HEIGHT * scale, GUTTER * scale
    rows = [bytearray(blank * w) for _ in range(h)]
    for _, cx, cy, cw, ch in CELLS:
        sx, sy = sheet_xy(cx, cy)
        bounds = (cx * scale, cy * scale, (cx + cw) * scale - 1, (cy + ch) * scale - 1)
        for j in range(-g, ch * scale + g):
            cj = min(max(cy * scale + j, bounds[1]), bounds[3])  # the gutter repeats the edge
            row = rows[sy * scale + j]
            for i in range(-g, cw * scale + g):
                ci = min(max(cx * scale + i, bounds[0]), bounds[2])
                t = (sx * scale + i) * len(blank)
                row[t:t + len(blank)] = texel(ci, cj, bounds)
    return rows


def write_png(path, width, height, channels, rows):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    color_type = {3: 2, 4: 6}[channels]
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(b"".join(b"\0" + bytes(r) for r in rows), 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    print("wrote %s (%dx%d, %d bytes)" % (os.path.relpath(path, ROOT), width, height, len(png)))


def main():
    write_png(OUTPUT, WIDTH, HEIGHT, 4,
              compose(1, bytes(4), lambda i, j, _: bytes(px[j][i])))
    write_png(NORMAL_OUTPUT, WIDTH * SCALE, HEIGHT * SCALE, 3,
              compose(SCALE, bytes((128, 128, 255)), lambda i, j, b: normal_at(i, j, *b)))
    for name, cx, cy, cw, ch in CELLS:
        print("  %-6s (%d, %d, %d, %d)" % (name, *sheet_xy(cx, cy), cw, ch))


if __name__ == "__main__":
    main()
