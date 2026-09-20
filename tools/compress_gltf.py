#!/usr/bin/env python3
"""Compress a glTF model's textures for GPUs (docs/PLAN-textures.md).

    tools/compress_gltf.py model.gltf        (or tools/compress_textures.sh --gltf model.gltf)

Compresses every image the model's textures use (tools/compress_textures.sh: name.bc7.ktx,
name.astc.ktx and name.etc2.ktx beside each) and writes model.ktx.gltf beside the model:
the same model, each texture given the WGR_texture_ktx extension pointing at its compressed
image ("name.ktx"). libwgrender loads the variant the GPU can use, else the texture's own image;
other glTF viewers ignore the extension (it's in extensionsUsed, not extensionsRequired)
and use the original images. The original model is left as it is.

Images that hold data (normal, metallic-roughness and occlusion maps) are compressed as
linear; colors (base color, emissive) as sRGB. Only .gltf files with images in separate
PNG or JPEG files: images inside the file (a .glb, data: URIs, buffer views) are left
uncompressed, with a note.
"""
import json
import os
import subprocess
import sys

EXTENSION = "WGR_texture_ktx"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPRESS = os.path.join(ROOT, "tools", "compress_textures.sh")

LINEAR_SLOTS = ("normalTexture", "occlusionTexture", "metallicRoughnessTexture")


def texture_uses(gltf):
    """texture index -> "linear" or "srgb", from how materials use it."""
    uses = {}

    def note(info, kind):
        if isinstance(info, dict) and "index" in info:
            prior = uses.get(info["index"])
            uses[info["index"]] = "srgb" if prior == "srgb" or kind == "srgb" else "linear"

    for material in gltf.get("materials", []):
        pbr = material.get("pbrMetallicRoughness", {})
        note(pbr.get("baseColorTexture"), "srgb")
        note(pbr.get("metallicRoughnessTexture"), "linear")
        note(material.get("emissiveTexture"), "srgb")
        note(material.get("normalTexture"), "linear")
        note(material.get("occlusionTexture"), "linear")
    return uses


def main():
    if len(sys.argv) != 2 or not sys.argv[1].endswith(".gltf"):
        sys.exit("usage: tools/compress_gltf.py model.gltf  (a .glb holds its images inside: not yet)")
    path = sys.argv[1]
    base = os.path.dirname(os.path.abspath(path))
    with open(path, encoding="utf-8") as f:
        gltf = json.load(f)

    textures, images = gltf.get("textures", []), gltf.get("images", [])
    uses = texture_uses(gltf)
    image_kind = {}  # image index -> "linear" / "srgb" (sRGB wins when used both ways)
    for t, texture in enumerate(textures):
        source = texture.get("source")
        if source is None:
            continue
        kind = uses.get(t, "srgb")
        image_kind[source] = "srgb" if image_kind.get(source) == "srgb" or kind == "srgb" else kind

    ktx_image = {}  # source image index -> its compressed image's index
    for index, kind in sorted(image_kind.items()):
        uri = images[index].get("uri", "")
        stem, ext = os.path.splitext(uri)
        if images[index].get("bufferView") is not None or uri.startswith("data:") or ext.lower() not in (
                ".png", ".jpg", ".jpeg"):
            print(f"compress_gltf: image {index} ({uri or 'inside the file'}): not a separate PNG or JPEG, left as is")
            continue
        file = os.path.join(base, uri)
        subprocess.run([COMPRESS] + (["--linear"] if kind == "linear" else []) + [file], check=True)
        ktx_image[index] = len(images)
        images.append({"uri": stem + ".ktx", "name": images[index].get("name", os.path.basename(stem)) + " (KTX)"})

    for texture in textures:
        if texture.get("source") in ktx_image:
            texture.setdefault("extensions", {})[EXTENSION] = {"source": ktx_image[texture["source"]]}
    if ktx_image:
        used = gltf.setdefault("extensionsUsed", [])
        if EXTENSION not in used:
            used.append(EXTENSION)

    out = path[: -len(".gltf")] + ".ktx.gltf"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(gltf, f, indent=1)
    print(f"compress_gltf: {len(ktx_image)} image(s) compressed; wrote {out}")


if __name__ == "__main__":
    main()
