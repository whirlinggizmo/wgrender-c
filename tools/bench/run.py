#!/usr/bin/env python3
"""Build a benchmark with CMake and run it.

    tools/bench/run.py NAME [--desktop] [--ktx]

  loadbench     the worst frame while loading Sponza and FlightHelmet, background vs
                synchronous (downloads the models the first time: fetch_assets.py)
  spritebench   sprite-heavy scenes: frame time, and CPU split into update / scene /
                submit, with sokol_gl's vertex and command use
  shadowbench   what a casting light costs a frame: no shadows, one light at two map
                sizes, two lights, one where nothing receives

Headless by default (CPU work only; no GPU at all), with this machine's headless preset
(linux-headless, ...); --desktop uses its release preset (linux-release, ...), with real GPU work in a window (vsync off). --ktx
has loadbench load the models' compressed textures (made the first time:
tools/compress_textures.py --gltf).

The web versions are targets of the web presets: cmake --build --preset web-webgl2
--target spritebench, then serve the site and open /bench/?ex=spritebench.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import builds  # noqa: E402

BENCH = ROOT / 'examples' / 'assets' / 'bench'


def run(*cmd, **kw):
    done = subprocess.run(cmd, cwd=ROOT, **kw)
    if done.returncode != 0:
        sys.exit(done.returncode)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('name', choices=['loadbench', 'spritebench', 'shadowbench'])
    ap.add_argument('--desktop', action='store_true')
    ap.add_argument('--ktx', action='store_true')
    args = ap.parse_args()
    preset = builds.native('release' if args.desktop else 'headless')

    env = dict(os.environ)
    if args.name == 'loadbench':
        run(sys.executable, str(ROOT / 'tools/bench/fetch_assets.py'))
        if args.ktx:
            for model in ('Sponza/Sponza', 'FlightHelmet/FlightHelmet'):
                if not (BENCH / f'{model}.ktx.gltf').exists():
                    run(sys.executable, str(ROOT / 'tools/compress_textures.py'), '--gltf',
                        str(BENCH / f'{model}.gltf'), stdout=subprocess.DEVNULL)
        env['WGR_LOADBENCH_KTX'] = '1' if args.ktx else '0'

    run('cmake', '--preset', preset, stdout=subprocess.DEVNULL)
    run('cmake', '--build', '--preset', preset, '--target', args.name)
    exe = builds.directory(preset) / (args.name + ('.exe' if os.name == 'nt' else ''))
    # the benchmarks report on stdout; wgrender's log goes to stderr
    sys.exit(subprocess.run([str(exe)], cwd=ROOT, env=env, stderr=subprocess.DEVNULL).returncode)


if __name__ == '__main__':
    main()
