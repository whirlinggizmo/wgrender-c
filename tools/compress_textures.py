#!/usr/bin/env python3
"""Compress textures for GPUs (docs/PLAN-textures.md): for each PNG, writes beside it

    name.bc7.ktx    BC7        desktops
    name.astc.ktx   ASTC 4x4   phones
    name.etc2.ktx   ETC2 RGBA  older phones

each with its mipmaps (JPEGs work too). A program loads "name.ktx" (wgr_texture_create,
or ensured through wgr_asset first) and libwgrender picks the file this GPU can use,
falling back to name.png. Keep the PNG: it's the fallback, and pixel-accurate picking
reads it.

    tools/compress_textures.py [--linear] image.png...
    tools/compress_textures.py --gltf model.gltf     (a model's textures: tools/compress_gltf.py)

--linear: the images hold data, not colors (normal maps, roughness): no sRGB weighting
when compressing, and mipmaps averaged as they are.

Encodes with Basis Universal (UASTC, level 2, then transcoded to each format), built the
first time from a pinned release into the per-user cache (tools/hostcache.py:
~/.cache/wgrender/tools on Linux; needs git, CMake and a C++ compiler). Only needed to make the files, never at runtime.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from hostcache import cache_dir  # noqa: E402

BASISU_TAG = '1.16.4'
ROOT = Path(__file__).resolve().parents[1]
TOOLS = cache_dir('tools') / f'basisu-{BASISU_TAG}'
# transcoder format numbers (basist::transcoder_texture_format) and our suffixes
FORMATS = [(6, 'bc7', 'BC7_RGBA'), (10, 'astc', 'ASTC_RGBA'), (1, 'etc2', 'ETC2_RGBA')]


def basisu():
    exe = 'basisu.exe' if os.name == 'nt' else 'basisu'
    found = [p for p in (TOOLS / 'basis_universal' / 'bin' / exe,
                         *(TOOLS / 'basisu-build').rglob(exe)) if p.is_file()]
    if found:
        return found[0]
    print(f'compress_textures: building basisu {BASISU_TAG} (once) into {TOOLS}', flush=True)
    TOOLS.mkdir(parents=True, exist_ok=True)
    src = TOOLS / 'basis_universal'
    if not src.is_dir():
        subprocess.run(['git', 'clone', '-q', '--depth', '1', '--branch', BASISU_TAG,
                        'https://github.com/BinomialLLC/basis_universal.git', str(src)], check=True)
    build = TOOLS / 'basisu-build'
    subprocess.run(['cmake', '-S', str(src), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release'],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(build), '--config', 'Release', '--target', 'basisu', '-j'],
                   check=True, stdout=subprocess.DEVNULL)
    return basisu()


def compress(tool, image, linear):
    if image.suffix.lower() not in ('.png', '.jpg', '.jpeg'):
        sys.exit(f'compress_textures: {image}: not a .png or .jpg')
    name = image.stem
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        shutil.copyfile(image, work / image.name)
        flags = ['-linear', '-mip_linear'] if linear else []
        done = subprocess.run([str(tool), '-uastc', '-uastc_level', '2', '-mipmap', *flags, image.name],
                              cwd=work, capture_output=True, text=True)
        if done.returncode != 0:
            print(f'compress_textures: {image}: encoding failed (see below)', file=sys.stderr)
            print('\n'.join((done.stdout + done.stderr).splitlines()[-5:]), file=sys.stderr)
            sys.exit(1)
        for number, suffix, label in FORMATS:
            subprocess.run([str(tool), '-unpack', '-ktx_only', '-format_only', str(number), f'{name}.basis'],
                           cwd=work, capture_output=True, check=True)
            shutil.move(work / f'{name}_transcoded_{label}_0000.ktx', image.with_name(f'{name}.{suffix}.ktx'))
    sizes = ', '.join(f'{s} {image.with_name(f"{name}.{s}.ktx").stat().st_size}' for _, s, _ in FORMATS)
    print(f'{image}: {image.stat().st_size} bytes -> {sizes}')


def main():
    args = sys.argv[1:]
    if args[:1] == ['--gltf']:
        sys.exit(subprocess.run([sys.executable, str(ROOT / 'tools' / 'compress_gltf.py'), *args[1:]]).returncode)
    linear = args[:1] == ['--linear']
    if linear:
        args = args[1:]
    if not args:
        sys.exit(__doc__)
    tool = basisu()
    for image in args:
        compress(tool, Path(image).resolve(), linear)


if __name__ == '__main__':
    main()
