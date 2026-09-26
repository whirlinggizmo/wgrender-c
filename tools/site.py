#!/usr/bin/env python3
"""A self-contained copy of a web build to hand a static host: the examples, the page,
and the assets they load, so nothing has to be mounted beside it (tools/serve.py does
that locally).

    tools/site.py [BUILD] [--out DIR]

BUILD is what a web preset made (default out/web/webgl2-nothreads); the copy goes to the
preset's work directory, build/web/<variant>/site, unless --out says otherwise, so the
build's own out/ stays just the build. Not the benchmarks (bench/ in the build, and
examples/assets/bench): a local tool, and loadbench's models are downloaded. The
copied assets get their manifests (tools/gen_manifest.py).

Use a -nothreads build for a host that can't send COOP/COEP headers (GitHub Pages): a
threaded build doesn't start at all there.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import builds  # noqa: E402

ROOT = builds.ROOT


def size(path):
    return sum(f.stat().st_size for f in path.rglob('*') if f.is_file())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('build', nargs='?', default=str(builds.out('web-webgl2-nothreads')))
    ap.add_argument('--out')
    args = ap.parse_args()
    build = Path(args.build).resolve()
    out = Path(args.out).resolve() if args.out else builds.work(builds.preset_of(build)) / 'site'
    if not (build / 'examples.json').exists():
        sys.exit(f'site: no web build at {build} (cmake --preset {builds.preset_of(build)} && '
                 f'cmake --build --preset {builds.preset_of(build)})')

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    files = ['index.html', 'examples.json', *(p.name for p in build.glob('*.js')),
             *(p.name for p in build.glob('*.wasm'))]
    for name in files:
        shutil.copy2(build / name, out / name)
    shutil.copytree(ROOT / 'examples' / 'assets', out / 'assets', ignore=shutil.ignore_patterns('bench'))
    # the manifests the examples set (EXAMPLE_ASSET_MANIFEST): a returning visitor then
    # fetches only the assets that changed since the last deploy
    subprocess.run([sys.executable, ROOT / 'tools' / 'gen_manifest.py', out / 'assets'], check=True)
    print(f'site: {out} ({size(out) / 1e6:.1f} MB): any static host, at a domain root or under a path')


if __name__ == '__main__':
    main()
