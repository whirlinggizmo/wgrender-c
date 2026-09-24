#!/usr/bin/env python3
"""Regenerate the committed shaders.

    tools/gen_shaders.py              the library's: src/shaders/*.glsl -> *.glsl.h
    tools/gen_shaders.py --examples   examples/shaders/*.glsl -> examples/assets/shaders/*.wgrshader

The library's shaders are compiled by sokol-shdc for GL core, WebGL2 and WebGPU, each
behind #if defined(SOKOL_<backend>) (--ifdef) so a build only carries its own; include
them through src/internal/wgr_shaders.h. sokol-shdc is downloaded the first time, for
this OS, at the sokol-tools-bin commit deps/sokol/VERSION pins, into the per-user
cache (tools/hostcache.py: ~/.cache/wgrender/tools on Linux).

The custom material shaders of examples/shaders.c are packed by tools/shaderpack.py;
rebuild them after changing one, or shaders/wgr.glsl.

CMake has the targets `gen-shaders` and `gen-example-shaders` for these.
"""
import platform
import stat
import subprocess
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from hostcache import cache_dir  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ['src/shaders/wgr_model.glsl', 'src/shaders/wgr_sprite.glsl', 'src/shaders/wgr_depth.glsl']
SLANG = 'glsl410:glsl300es:wgsl'


def shdc():
    pin = next(l.split()[1] for l in (ROOT / 'deps/sokol/VERSION').read_text().splitlines()
               if l.startswith('sokol-tools-bin '))
    system, machine = platform.system(), platform.machine().lower()
    folder = {'Linux': 'linux_arm64' if machine in ('aarch64', 'arm64') else 'linux',
              'Darwin': 'osx_arm64' if machine == 'arm64' else 'osx',
              'Windows': 'win32'}[system]
    exe = 'sokol-shdc.exe' if system == 'Windows' else 'sokol-shdc'
    path = cache_dir('tools') / f'sokol-shdc-{pin[:12]}' / exe
    if not path.exists():
        url = f'https://raw.githubusercontent.com/floooh/sokol-tools-bin/{pin}/bin/{folder}/{exe}'
        print(f'gen_shaders: downloading {url}', flush=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=120) as r:
            path.write_bytes(r.read())
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


def main():
    if sys.argv[1:] == ['--examples']:
        for glsl in sorted((ROOT / 'examples/shaders').glob('*.glsl')):
            out = ROOT / 'examples/assets/shaders' / f'{glsl.stem}.wgrshader'
            subprocess.run([sys.executable, str(ROOT / 'tools/shaderpack.py'), str(glsl), '-o', str(out)],
                           check=True)
        return
    if sys.argv[1:]:
        sys.exit(__doc__)
    tool = shdc()
    for s in SHADERS:
        print(f'  shdc: {s}', flush=True)
        subprocess.run([str(tool), '-i', s, '-o', f'{s}.h', '-l', SLANG, '--ifdef'], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
