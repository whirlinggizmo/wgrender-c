#!/usr/bin/env python3
"""What the Haxe guest ports cost against wgrender's own C builds of the same example.

    tools/compare.py [DIR ...]      default: the sibling example directories

Like against like: both sides are the same wgrender, the same backend and the same
threading (WEB_THREADS=0), so the only difference is the language the game is written
in. Build the C side first:

    cmake --preset web-webgl2-nothreads && cmake --build --preset web-webgl2-nothreads   (in wgrender-c)

What a visitor downloads is what is measured -- wasm plus JS, and the gzipped total,
since that is what crosses the wire. The Haxe side has a third file: the host wasm is
wgrender, the host JS is Emscripten's glue, and the guest JS is the game.

The host shrinks per example. It exports exactly the wgrender calls the compiled guest
makes, so an example that never touches audio or particles does not carry them.
"""
import gzip
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wgrpath import find  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, (), positional=None)
LIB = pathlib.Path(__file__).resolve().parent.parent
# argv here is example directories, so wgrender comes from the environment or
# the usual places -- not from a positional that means something else.
WGRENDER = find(argv=[])
GUEST = 'js-webgl2-nothreads'  # the guest builds compared: tools/guestbuild.py's default
C_BUILD = WGRENDER / 'out/web/webgl2-nothreads'


def measure(*paths):
    raw = zipped = 0
    for p in paths:
        if not p.exists():
            return None
        data = p.read_bytes()
        raw += len(data)
        zipped += len(gzip.compress(data, 9))
    return raw, zipped


def main():
    dirs = [pathlib.Path(a).resolve() for a in sys.argv[1:]]
    if not dirs:
        dirs = sorted(d for d in (LIB / 'examples').iterdir()
                      if (d / 'build.py').exists() and (d / 'out/web' / GUEST).exists())
    if not C_BUILD.exists():
        sys.exit(f'no C builds to compare against at {C_BUILD}\n'
                 f'  cd {WGRENDER} && cmake --preset web-webgl2-nothreads && '
                 'cmake --build --preset web-webgl2-nothreads')
    rows = []
    for d in dirs:
        name = d.name
        site = d / 'out/web' / GUEST
        hx = measure(site / 'wgrender-host.wasm', site / 'wgrender-host.js', site / f'{name}.js')
        c = measure(C_BUILD / f'{name}.wasm', C_BUILD / f'{name}.js')
        if hx is None:
            print(f'{name}: not built (./build.py all)', file=sys.stderr)
            continue
        if c is None:
            print(f'{name}: wgrender has no C build of it', file=sys.stderr)
            continue
        guest = measure(site / f'{name}.js')
        rows.append((name, c, hx, guest))
    if not rows:
        sys.exit('nothing to compare')

    print(f'{"example":<12} {"C total":>10} {"gz":>9} {"Haxe total":>11} {"gz":>9} '
          f'{"vs C":>6} {"gz":>6} {"guest js":>9} {"gz":>8}')
    for name, c, hx, guest in rows:
        print(f'{name:<12} {c[0]:>10,} {c[1]:>9,} {hx[0]:>11,} {hx[1]:>9,} '
              f'{hx[0] / c[0]:>5.2f}x {hx[1] / c[1]:>5.2f}x {guest[0]:>9,} {guest[1]:>8,}')

    over = [(hx[0] - c[0], hx[1] - c[1]) for _, c, hx, _ in rows]
    print(f'\nThe Haxe port costs {min(o[0] for o in over):,} to {max(o[0] for o in over):,} bytes '
          f'more than the C ({min(o[1] for o in over):,} to {max(o[1] for o in over):,} gzipped),')
    print('which is the guest JS: the host wasm is the same wgrender either way.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
