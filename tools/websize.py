#!/usr/bin/env python3
"""Web build sizes per example: wasm and JS glue, raw and compressed (gzip -9, and
brotli when Python's brotli module or the brotli tool is installed), sorted by total download size.

    tools/websize.py [BUILD] [--summary]

BUILD is what a web preset made (default out/web/webgl2). Writes the table to the
preset's work directory as well (build/web/<variant>/sizes.txt, not in the site);
--summary prints only hello, simple and model.
"""
import argparse
import gzip
import shutil
import subprocess
import sys
from pathlib import Path

try:
    import brotli as brotli_module
except ImportError:
    brotli_module = None
BROTLI_TOOL = shutil.which('brotli')
brotli = brotli_module is not None or BROTLI_TOOL is not None


def brotli_size(data):
    if brotli_module:
        return len(brotli_module.compress(data, quality=11))
    return len(subprocess.run([BROTLI_TOOL, '-q', '11', '-c'], input=data, capture_output=True).stdout)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import builds  # noqa: E402


def kb(n):
    return f'{n / 1024:.1f}'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('build', nargs='?', default=str(builds.out('web-webgl2')))
    ap.add_argument('--summary', action='store_true')
    args = ap.parse_args()
    site = Path(args.build)

    rows = []
    for js in sorted(site.glob('*.js')):
        wasm = js.with_suffix('.wasm')
        if not wasm.exists():
            continue
        w, j = wasm.read_bytes(), js.read_bytes()
        gz = [len(gzip.compress(b, 9)) for b in (w, j)]
        br = [brotli_size(b) for b in (w, j)] if brotli else [0, 0]
        rows.append((js.stem, len(w), len(j), *gz, *br, sum(gz), sum(br)))
    if not rows:
        sys.exit(f'websize: no examples in {site}')
    rows.sort(key=lambda r: r[7])

    head = ['example', 'wasm', 'js', 'wasm.gz', 'js.gz'] + (['wasm.br', 'js.br'] if brotli else []) \
        + ['gz total'] + (['br total'] if brotli else [])
    lines = [f'web sizes: {site} (KB)', f'{head[0]:<16}' + ''.join(f'{h:>10}' for h in head[1:])]
    for name, w, j, wg, jg, wb, jb, gt, bt in rows:
        cols = [w, j, wg, jg] + ([wb, jb] if brotli else []) + [gt] + ([bt] if brotli else [])
        lines.append(f'{name:<16}' + ''.join(f'{kb(c):>10}' for c in cols))
    if not brotli:
        lines.append('(install brotli, the tool or the Python module, for brotli sizes)')
    table = builds.work(builds.preset_of(site)) / 'sizes.txt'
    table.parent.mkdir(parents=True, exist_ok=True)
    table.write_text('\n'.join(lines) + '\n')
    if args.summary:
        lines = lines[1:2] + [l for l in lines[2:] if l.split()[0] in ('hello', 'simple', 'model')] \
            + [f'(all examples: {table})']
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
