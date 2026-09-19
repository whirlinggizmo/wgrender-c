#!/usr/bin/env python3
"""Generate examples/assets/textures/noise.png: tileable value noise (four octaves),
128x128 grayscale, for the dissolve shader in the `shaders` example. Needs nothing but
the standard library. Run from anywhere:

    tools/gen_noise.py
"""
import os
import random
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, "examples/assets/textures/noise.png")
SIZE = 128


def octave(cells, rng):
    grid = [[rng.random() for _ in range(cells)] for _ in range(cells)]

    def at(x, y):  # smooth interpolation between lattice values, wrapping at the edges
        fx, fy = x * cells / SIZE, y * cells / SIZE
        x0, y0 = int(fx) % cells, int(fy) % cells
        x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
        tx, ty = fx - int(fx), fy - int(fy)
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        top = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * tx
        bottom = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * tx
        return top + (bottom - top) * ty

    return at


def main():
    rng = random.Random(7)
    octaves = [(octave(cells, rng), weight) for cells, weight in ((4, 0.5), (8, 0.25), (16, 0.15), (32, 0.1))]
    values = [[sum(f(x, y) * w for f, w in octaves) for x in range(SIZE)] for y in range(SIZE)]
    low = min(min(r) for r in values)
    high = max(max(r) for r in values)
    rows = []
    for y in range(SIZE):
        row = bytearray([0])  # filter: none
        for x in range(SIZE):
            v = (values[y][x] - low) / (high - low)  # stretched to the full range
            row.append(max(0, min(255, int(round(v * 255)))))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 0, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))
    with open(OUTPUT, "wb") as f:
        f.write(png)
    print(f"wrote {OUTPUT} ({len(png)} bytes)")


if __name__ == "__main__":
    main()
