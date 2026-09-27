#!/usr/bin/env python3
"""wgrender-hx against the C: the `simple` example as a Haxe -> JS guest and as
Haxe -> hxcpp in one wasm, beside wgrender's own C build of it.

    tools/benchmarks.py          build both, measure them, write bench/results.json
                                 and docs/benchmarks.md
    tools/benchmarks.py --doc    only regenerate docs/benchmarks.md

The harness is wgrender's (tools/bench/measure.py, found the way every tool here
finds wgrender: tools/wgrpath.py), and so is the C baseline: run wgrender's
tools/benchmarks.py first, on the same machine, so its bench/results.json is there
to compare against. wgrender's docs/benchmarks.md collects this project's results
from a sibling checkout.

Run by hand, not in CI: it drives a browser for about a minute per configuration.
Commit bench/results.json and docs/benchmarks.md afterwards. bench/notes.md is the
hand-written part of the page; edit it, then --doc.
"""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from wgrpath import find  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--doc',), positional=0)

WGRENDER = find(argv=[])
sys.path.insert(0, str(WGRENDER / 'tools/bench'))
import measure  # noqa: E402

RESULTS = ROOT / 'bench/results.json'
DOC = ROOT / 'docs/benchmarks.md'
EXAMPLES = ROOT / 'examples'


def source():
    if os.environ.get('WGRENDER_DIR'):
        return 'WGRENDER_DIR'
    return 'submodule' if WGRENDER == (ROOT / 'project/lib/wgrender-c').resolve() else 'sibling checkout'


def version(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout.strip().splitlines()[0]


def measure_all():
    env = dict(measure.WEB_VARS, WGRENDER_DIR=str(WGRENDER))
    measure.run([sys.executable, EXAMPLES / 'build.py', 'web', 'simple', 'stress'], cwd=EXAMPLES, env=env)
    measure.run([sys.executable, 'build.py', 'web'], cwd=EXAMPLES / 'simple-hxcpp', env=env)
    # the stress scene all-in-one through hxcpp: the Haxe GC inside the wasm, which
    # gcbench cannot trace but a late frame shows
    measure.run([sys.executable, ROOT / 'tools/hxcppweb.py', 'stress'], cwd=ROOT, env=env)

    haxe = f'Haxe {version(["haxe", "--version"])}'
    hxcpp = version(['haxelib', 'list', 'hxcpp']).split('[')[0].replace(':', '').strip()

    guest = EXAMPLES / 'simple/out/web/js-webgl2-nothreads'  # measure.WEB_VARS: no threads
    js = {
        'id': 'haxe-js', 'label': 'Haxe -> JS guest', 'project': 'wgrender-hx', 'example': 'simple',
        'toolchain': haxe,
        # the page is wgr.macros.WebHost's: index.html and boot.js, which loads host and guest
        'sizes': measure.sizes([guest / 'wgrender-host.wasm', guest / 'wgrender-host.js', guest / 'simple.js',
                                guest / 'index.html', guest / 'boot.js']),
        'frame': measure.frame(guest, 'haxe-js'),
        'gc': measure.gc(guest, 'haxe-js'),
        'calls': measure.calls(guest, 'haxe-js'),
        'stress': measure.stress(EXAMPLES / 'stress/out/web/js-webgl2-nothreads', 'haxe-js', '/?n={n}'),
    }
    native = EXAMPLES / 'simple-hxcpp/out/web/webgl2-nothreads'
    page = {'probe': 'simple.js'}
    cpp = {
        'id': 'haxe-hxcpp', 'label': 'Haxe -> hxcpp', 'project': 'wgrender-hx', 'example': 'simple',
        'toolchain': f'{haxe}, {hxcpp}',
        # the page is wgrender's example shell, which fetches examples.json for its picker
        'sizes': measure.sizes([native / 'simple.wasm', native / 'simple.js',
                                native / 'index.html', native / 'examples.json']),
        'frame': measure.frame(native, 'haxe-hxcpp', **page),
        'gc': measure.gc(native, 'haxe-hxcpp', **page),
        'stress': measure.stress(EXAMPLES / 'stress/out/web/hxcpp-webgl2-nothreads', 'haxe-hxcpp', '/?n={n}', 'stress.js'),
    }
    return measure.write_results(RESULTS, 'wgrender-hx', measure.wgrender_info(WGRENDER, source()),
                                 [js, cpp])


def main():
    baseline_path = WGRENDER / 'bench/results.json'
    if not baseline_path.is_file():
        sys.exit(f'no C baseline at {baseline_path}: run {WGRENDER / "tools/benchmarks.py"} first')
    baseline = measure.load_results(baseline_path)
    ours = measure.load_results(RESULTS) if '--doc' in sys.argv[1:] else measure_all()
    lead = ('`simple` as a Haxe guest running as JS against wgrender\'s wasm, and as Haxe '
            'compiled through hxcpp into one wasm, beside the C. The C row and the call '
            'costs are wgrender\'s baseline (its `bench/results.json`); every binding is '
            'collected in wgrender\'s `docs/benchmarks.md`.')
    DOC.write_text(measure.render_doc('wgrender-hx benchmarks', lead, [baseline, ours], baseline,
                                      'tools/benchmarks.py', measure.read_notes(ROOT)), encoding='utf-8')
    print(f'wrote {DOC}')


if __name__ == '__main__':
    main()
