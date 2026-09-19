#!/usr/bin/env bash
# Compress textures for GPUs (docs/PLAN-textures.md): for each PNG, writes beside it
#
#   name.bc7.ktx    BC7        desktops
#   name.astc.ktx   ASTC 4x4   phones
#   name.etc2.ktx   ETC2 RGBA  older phones
#
# each with its mipmaps. A program loads "name.ktx" (sk_texture_create, or ensured
# through sk_asset first) and libsk picks the file this GPU can use, falling back to
# name.png. Keep the PNG: it's the fallback, and pixel-accurate picking reads it.
#
#   tools/compress_textures.sh [--linear] image.png...
#
# --linear: the images hold data, not colors (normal maps, roughness): no sRGB
# weighting when compressing, and mipmaps averaged as they are.
#
# Encodes with Basis Universal (UASTC, level 2, then transcoded to each format), built
# the first time from a pinned release into build/tools (needs git, CMake and a C++
# compiler). Only needed to make the files, never at runtime.
set -eu

BASISU_TAG=1.16.4
root="$(cd "$(dirname "$0")/.." && pwd)"
tools="$root/build/tools"
basisu="$tools/basis_universal/bin/basisu"

linear=()
if [ "${1:-}" = "--linear" ]; then
    linear=(-linear -mip_linear)
    shift
fi
if [ $# -eq 0 ]; then
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi

if [ ! -x "$basisu" ]; then
    echo "compress_textures: building basisu $BASISU_TAG (once) into build/tools"
    mkdir -p "$tools"
    if [ ! -d "$tools/basis_universal" ]; then
        git clone -q --depth 1 --branch "$BASISU_TAG" https://github.com/BinomialLLC/basis_universal.git \
            "$tools/basis_universal"
    fi
    cmake -S "$tools/basis_universal" -B "$tools/basisu-build" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$tools/basisu-build" --target basisu -j >/dev/null
fi

# transcoder format numbers (basist::transcoder_texture_format) and our suffixes
formats=("6:bc7:BC7_RGBA" "10:astc:ASTC_RGBA" "1:etc2:ETC2_RGBA")

for png in "$@"; do
    case "$png" in
        *.png) ;;
        *) echo "compress_textures: $png: not a .png" >&2; exit 1 ;;
    esac
    stem="${png%.png}"
    name="$(basename "$stem")"
    work="$(mktemp -d "$tools/compress.XXXXXX")"
    trap 'rm -rf "$work"' EXIT
    cp "$png" "$work/$name.png"
    (cd "$work" && "$basisu" -uastc -uastc_level 2 -mipmap "${linear[@]}" "$name.png" >encode.log 2>&1) || {
        echo "compress_textures: $png: encoding failed (see below)" >&2
        tail -n 5 "$work/encode.log" >&2
        exit 1
    }
    for f in "${formats[@]}"; do
        IFS=: read -r number suffix label <<<"$f"
        (cd "$work" && "$basisu" -unpack -ktx_only -format_only "$number" "$name.basis" >unpack.log 2>&1)
        mv "$work/${name}_transcoded_${label}_0000.ktx" "$stem.$suffix.ktx"
    done
    printf '%s: %s bytes -> bc7 %s, astc %s, etc2 %s\n' "$png" "$(stat -c%s "$png")" \
        "$(stat -c%s "$stem.bc7.ktx")" "$(stat -c%s "$stem.astc.ktx")" "$(stat -c%s "$stem.etc2.ktx")"
    rm -rf "$work"
    trap - EXIT
done
