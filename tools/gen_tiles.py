#!/usr/bin/env python3
"""Generate examples/assets/textures/tiles.png: the sprite sheet the `2d` example
draws its world from. 64x48 RGBA, laid out as

    (0,0) grass   (16,0) sand   (32,0) water   (48,0) stone   16x16, opaque
    (0,16) tree   (16,16) flag                                16x32, transparent
                                (32,16) coin   (48,16) rock   16x16, transparent

Pixel art on purpose: the example samples it with nearest filtering. Needs nothing
but the standard library. Run from anywhere:

    tools/gen_tiles.py
"""
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, "examples/assets/textures/tiles.png")

WIDTH, HEIGHT = 64, 48
px = [[(0, 0, 0, 0)] * WIDTH for _ in range(HEIGHT)]


def put(x, y, color):
    if 0 <= x < WIDTH and 0 <= y < HEIGHT and color[3] > 0:
        px[y][x] = color


def rect(x, y, w, h, color):
    for j in range(h):
        for i in range(w):
            put(x + i, y + j, color)


def disc(cx, cy, r, color):
    for j in range(int(cy - r), int(cy + r) + 1):
        for i in range(int(cx - r), int(cx + r) + 1):
            if (i + 0.5 - cx) ** 2 + (j + 0.5 - cy) ** 2 <= r * r:
                put(i, j, color)


def noise(x, y, w, h, color, step, offset=0):
    """A sparse, repeatable speckle so flat tiles don't look like plain fills.
    A hash, not a linear pattern: `i * 7 + j * 13` draws visible diagonal stripes."""
    for j in range(h):
        for i in range(w):
            hashed = ((i + offset) * 73856093) ^ ((j + offset) * 19349663)
            if (hashed >> 4) % step == 0:
                put(x + i, y + j, color)


# --- terrain (16x16, opaque) ---
rect(0, 0, 16, 16, (86, 138, 74, 255))          # grass
noise(0, 0, 16, 16, (104, 158, 88, 255), 5)
noise(0, 0, 16, 16, (72, 118, 64, 255), 7, 3)

rect(16, 0, 16, 16, (204, 184, 130, 255))       # sand
noise(16, 0, 16, 16, (222, 204, 154, 255), 5)
noise(16, 0, 16, 16, (186, 164, 112, 255), 9, 4)

rect(32, 0, 16, 16, (58, 104, 160, 255))        # water
for j in range(0, 16, 4):                       # wave lines
    for i in range(16):
        if (i + j) % 8 < 4:
            put(32 + i, j + 1, (86, 138, 194, 255))

rect(48, 0, 16, 16, (128, 126, 124, 255))       # stone
noise(48, 0, 16, 16, (150, 148, 146, 255), 4)
for i in range(16):                             # a crack
    put(48 + i, (i * 5 // 8 + 3) % 16, (98, 96, 94, 255))

# --- tall objects (16x32, transparent background) ---
rect(6, 16 + 18, 4, 14, (96, 68, 44, 255))      # tree trunk
disc(8, 16 + 14, 7.5, (54, 106, 58, 255))       # canopy
disc(6, 16 + 12, 4.5, (72, 130, 70, 255))       # lit side

rect(23, 16 + 2, 2, 30, (168, 168, 172, 255))   # flag pole
for j in range(7):                              # pennant (stays inside the 16 px cell)
    rect(25, 16 + 3 + j, 7 - j, 1, (196, 74, 66, 255))

# --- small objects (16x16, transparent background) ---
disc(40, 16 + 8, 6.0, (214, 172, 54, 255))      # coin
disc(40, 16 + 8, 3.5, (240, 212, 108, 255))
disc(56, 16 + 9, 5.5, (124, 120, 116, 255))     # rock
disc(54, 16 + 8, 3.0, (150, 146, 142, 255))


def main():
    rows = []
    for y in range(HEIGHT):
        row = bytearray([0])  # PNG filter type 0 (none)
        for x in range(WIDTH):
            row += bytes(px[y][x])
        rows.append(bytes(row))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) +
           chunk(b"IEND", b""))
    with open(OUTPUT, "wb") as f:
        f.write(png)
    print("wrote %s (%d bytes)" % (os.path.relpath(OUTPUT, ROOT), len(png)))


if __name__ == "__main__":
    main()
