#!/usr/bin/env python3
"""Generate the procedural assets used by examples/materials.c (stdlib only).

    examples/assets/models/sphere/sphere.glb      UV sphere: positions, normals,
                                                  texture coordinates (no tangents,
                                                  so libwgrender generates them), one
                                                  default material
    examples/assets/textures/tiles_normal.png     tangent-space normal map of
                                                  bevelled tiles

Usage: tools/gen_material_assets.py   (from the repository root)
"""
import json
import math
import struct
import zlib

SEGMENTS, RINGS, RADIUS = 48, 24, 0.5
NORMAL_SIZE, TILES, BEVEL = 256, 4, 0.18


def sphere_glb(path):
    positions, normals, uvs, indices = [], [], [], []
    for r in range(RINGS + 1):
        v = r / RINGS
        theta = v * math.pi
        for s in range(SEGMENTS + 1):
            u = s / SEGMENTS
            phi = u * 2.0 * math.pi
            n = (math.sin(theta) * math.cos(phi), math.cos(theta), -math.sin(theta) * math.sin(phi))
            normals += n
            positions += [c * RADIUS for c in n]
            uvs += (u, v)
    for r in range(RINGS):
        for s in range(SEGMENTS):
            a = r * (SEGMENTS + 1) + s
            b = a + SEGMENTS + 1
            indices += (a, b, a + 1, a + 1, b, b + 1)  # counter-clockwise from outside

    def pad4(data, fill=b"\0"):
        return data + fill * ((4 - len(data) % 4) % 4)

    chunks = [
        struct.pack(f"<{len(positions)}f", *positions),
        struct.pack(f"<{len(normals)}f", *normals),
        struct.pack(f"<{len(uvs)}f", *uvs),
        struct.pack(f"<{len(indices)}I", *indices),
    ]
    views, offset, binary = [], 0, b""
    for data in chunks:
        data = pad4(data)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data)})
        binary += data
        offset += len(data)
    vcount = len(positions) // 3
    gltf = {
        "asset": {"version": "2.0", "generator": "libwgrender tools/gen_material_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "sphere"}],
        "materials": [{"name": "sphere", "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.5}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                    "indices": 3, "material": 0}]}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [dict(v, target=34962) for v in views[:3]] + [dict(views[3], target=34963)],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": vcount, "type": "VEC3",
             "min": [-RADIUS] * 3, "max": [RADIUS] * 3},
            {"bufferView": 1, "componentType": 5126, "count": vcount, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": vcount, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
    }
    js = pad4(json.dumps(gltf, separators=(",", ":")).encode(), b" ")
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + binary)


def height(x, y):
    """Bevelled tiles: 1 on a tile's flat top, sloping to 0 at the grout lines."""
    fx, fy = (x * TILES) % 1.0, (y * TILES) % 1.0
    edge = min(fx, 1.0 - fx, fy, 1.0 - fy)
    return min(edge / BEVEL, 1.0)


def normal_png(path):
    size, rows = NORMAL_SIZE, []
    strength = 0.08  # height units per texel step, relative to the tile size
    for py in range(size):
        row = bytearray([0])  # PNG filter: none
        for px in range(size):
            x, y, d = (px + 0.5) / size, (py + 0.5) / size, 1.0 / size
            dx = (height(x + d, y) - height(x - d, y)) / (2 * d)
            dy = (height(x, y + d) - height(x, y - d)) / (2 * d)
            # glTF normal maps: +x right, +y up (texture v runs down, so negate dy)
            n = (-dx * strength, dy * strength, 1.0)
            length = math.sqrt(sum(c * c for c in n))
            row += bytes(int(round((c / length * 0.5 + 0.5) * 255)) for c in n)
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(b"".join(rows), 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    sphere_glb("examples/assets/models/sphere/sphere.glb")
    normal_png("examples/assets/textures/tiles_normal.png")
    print("wrote examples/assets/models/sphere/sphere.glb, examples/assets/textures/tiles_normal.png")
