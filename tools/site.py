#!/usr/bin/env python3
"""A self-contained copy of a web build to hand a static host: the examples, the page,
and the assets they load, so nothing has to be mounted beside it (tools/serve.py does
that locally).

    tools/site.py [BUILD] [--out DIR]

BUILD is a web preset's build directory (default build/web-webgl2-nothreads); the site
goes to BUILD/site unless --out says otherwise. Not the benchmarks (bench/ in the
build, and examples/assets/bench): a local tool, and loadbench's models are downloaded.

Use a -nothreads build for a host that can't send COOP/COEP headers (GitHub Pages): a
threaded build doesn't start at all there.
"""
import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def size(path):
    return sum(f.stat().st_size for f in path.rglob('*') if f.is_file())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('build', nargs='?', default=str(ROOT / 'build' / 'web-webgl2-nothreads'))
    ap.add_argument('--out')
    args = ap.parse_args()
    build = Path(args.build).resolve()
    out = Path(args.out).resolve() if args.out else build / 'site'
    if not (build / 'examples.json').exists():
        sys.exit(f'site: no web build at {build} (cmake --preset {build.name} && '
                 f'cmake --build --preset {build.name})')

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    files = ['index.html', 'examples.json', *(p.name for p in build.glob('*.js')),
             *(p.name for p in build.glob('*.wasm'))]
    for name in files:
        shutil.copy2(build / name, out / name)
    shutil.copytree(ROOT / 'examples' / 'assets', out / 'assets', ignore=shutil.ignore_patterns('bench'))
    print(f'site: {out} ({size(out) / 1e6:.1f} MB): any static host, at a domain root or under a path')


if __name__ == '__main__':
    main()
