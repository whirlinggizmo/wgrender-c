#!/usr/bin/env python3
"""Generate the particle textures the `particles` example draws with:

    examples/assets/textures/particle.png  64x64: a soft white dot (alpha falls off
                                           smoothly to the edge), tinted by emitters
    examples/assets/textures/flame.png     256x256: a 4x4 flipbook, 64x64 frames, of a
                                           white puff that grows and breaks up (left to
                                           right, top to bottom); emitters color it
                                           over its life (wgr_emitter3d_set_frames)

White on purpose: an emitter's color (and palette) tints them. Needs nothing but the
standard library. Run from anywhere:

    tools/gen_particles.py
"""
import math
import os
import random
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "examples/assets/textures")


def write_png(path, width, height, alpha_at):
    """A white RGBA PNG whose alpha is alpha_at(x, y) in 0..1."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # no filter
        for x in range(width):
            a = max(0, min(255, int(round(alpha_at(x, y) * 255))))
            rows += bytes((255, 255, 255, a))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    print(f"{os.path.relpath(path, ROOT)}: {len(png)} bytes")


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def dot(x, y):
    """The soft dot: full in the middle, gone at the edge."""
    d = math.hypot(x + 0.5 - 32.0, y + 0.5 - 32.0) / 32.0
    return 1.0 - smoothstep(0.0, 1.0, d)


# value noise on a lattice, smoothly interpolated, a few octaves
random.seed(7)
LATTICE = [[random.random() for _ in range(64)] for _ in range(64)]


def value_noise(x, y):
    xi, yi = math.floor(x), math.floor(y)
    fx, fy = x - xi, y - yi
    sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)

    def at(i, j):
        return LATTICE[j % 64][i % 64]

    top = at(xi, yi) + (at(xi + 1, yi) - at(xi, yi)) * sx
    bottom = at(xi, yi + 1) + (at(xi + 1, yi + 1) - at(xi, yi + 1)) * sx
    return top + (bottom - top) * sy


def fbm(x, y):
    return (value_noise(x, y) * 0.55 + value_noise(x * 2.1, y * 2.1) * 0.3 + value_noise(x * 4.3, y * 4.3) * 0.15)


FRAMES, CELL = 16, 64


def flame(x, y):
    frame = (y // CELL) * 4 + x // CELL
    t = frame / (FRAMES - 1)  # 0 at birth .. 1 at death
    cx, cy = x % CELL + 0.5 - 32.0, y % CELL + 0.5 - 32.0
    radius = 20.0 + 11.0 * t
    body = 1.0 - math.hypot(cx, cy * 0.85) / radius  # a little taller than wide
    # the noise drifts up as it ages, and eats more of the puff
    n = fbm((cx + 64.0) * 0.08, (cy + 64.0) * 0.08 + t * 2.5 + frame * 0.37)
    a = body * 1.2 - (0.25 + 0.6 * t) * (1.0 - n)
    return smoothstep(0.0, 0.6, a) * (1.0 - 0.3 * t)


write_png(os.path.join(TEXTURES, "particle.png"), 64, 64, dot)
write_png(os.path.join(TEXTURES, "flame.png"), CELL * 4, CELL * 4, flame)
