#!/usr/bin/env python3
"""Regenerate the committed shaders.

    tools/gen_shaders.py              the library's: src/shaders/*.glsl -> *.glsl.h
    tools/gen_shaders.py --examples   examples/shaders/*.glsl -> examples/assets/shaders/*.wgrshader

The library's shaders are compiled by sokol-shdc for GL core, WebGL2 and WebGPU, each
behind #if defined(SOKOL_<backend>) (--ifdef) so a build only carries its own; include
them through src/internal/wgr_shaders.h. sokol-shdc is downloaded the first time, for
this OS, at the sokol-tools-bin commit deps/sokol/VERSION pins, into the per-user
cache (tools/hostcache.py: ~/.cache/wgrender/tools on Linux).

The custom material shaders of examples/shaders.c are packed by tools/pack_shader.py;
rebuild them after changing one, or shaders/wgr.glsl.

CMake has the targets `gen-shaders` and `gen-example-shaders` for these.
"""
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import shdc  # noqa: E402  (sokol-shdc, pinned, fetched once)

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ['src/shaders/wgr_model.glsl', 'src/shaders/wgr_sprite.glsl', 'src/shaders/wgr_depth.glsl']
SLANG = 'glsl410:glsl300es:wgsl'


def main():
    if sys.argv[1:] == ['--examples']:
        for glsl in sorted((ROOT / 'examples/shaders').glob('*.glsl')):
            out = ROOT / 'examples/assets/shaders' / f'{glsl.stem}.wgrshader'
            subprocess.run([sys.executable, str(ROOT / 'tools/pack_shader.py'), str(glsl), '-o', str(out)],
                           check=True)
        return
    if sys.argv[1:]:
        sys.exit(__doc__)
    tool = shdc.path()
    for s in SHADERS:
        print(f'  shdc: {s}', flush=True)
        subprocess.run([str(tool), '-i', s, '-o', f'{s}.h', '-l', SLANG, '--ifdef'], cwd=ROOT, check=True)


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
    import cli  # noqa: E402  (tools/cli.py: --help, and no argument it doesn't take)
    cli.parse(__doc__, ('--examples',), positional=0)
    main()
