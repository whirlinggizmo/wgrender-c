#!/usr/bin/env python3
"""wgrender's benchmarks: the C baseline, and every binding's numbers beside it.

    tools/benchmarks.py          build the web examples, measure the C `simple`,
                                 write bench/results.json and docs/benchmarks.md
    tools/benchmarks.py --doc    only regenerate docs/benchmarks.md from the results
                                 files already there

Run by hand, not in CI: it drives a browser for about a minute. Commit
bench/results.json and docs/benchmarks.md afterwards.

A binding measures itself with its own tools/benchmarks.py, which uses this
repository's tools/bench/ (measure.py) and writes the binding's bench/results.json.
The doc here reads those from sibling checkouts (../wgrender-hx, ../wgrender-nim,
../wgrender-beef) when they are there and lists the ones that are not; it builds
none of them. A binding measured on another machine or against another wgrender
commit is flagged in the doc rather than left out.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/bench'))
import measure  # noqa: E402

RESULTS = ROOT / 'bench/results.json'
DOC = ROOT / 'docs/benchmarks.md'
BINDINGS = ['wgrender-hx', 'wgrender-nim', 'wgrender-beef']
EXAMPLE = 'simple'


def measure_c():
    measure.run(['make', 'wasm', '-j8'] + [f'{k}={v}' for k, v in measure.WEB_VARS.items()], cwd=ROOT)
    site = ROOT / 'examples/build/webgl2-nothreads'
    page = {'url': f'/?ex={EXAMPLE}', 'probe': f'{EXAMPLE}.js'}
    c = {
        'id': 'c', 'label': 'C', 'project': 'wgrender-c', 'example': EXAMPLE,
        'toolchain': f'Emscripten {measure.environment()["emcc"]}',
        'sizes': measure.sizes([site / f'{EXAMPLE}.wasm', site / f'{EXAMPLE}.js']),
        'frame': measure.frame(site, 'c', **page),
        'gc': measure.gc(site, 'c', **page),
    }
    return measure.write_results(RESULTS, 'wgrender-c', measure.wgrender_info(ROOT, 'self'), [c],
                                 {'callbench': measure.callbench()})


def write_doc(baseline):
    results, missing = [baseline], []
    for name in BINDINGS:
        path = ROOT.parent / name / 'bench/results.json'
        if path.is_file():
            results.append(measure.load_results(path))
        else:
            missing.append(name)
    lead = ('Every binding against the C, on the same example. Each project measures itself '
            '(its `tools/benchmarks.py`) with the harness in `tools/bench/`, and this page '
            'collects what they recorded.')
    if missing:
        lead += ' Not collected, no results found beside this checkout: ' + ', '.join(missing) + '.'
    DOC.write_text(measure.render_doc('wgrender benchmarks', lead, results, baseline,
                                      'tools/benchmarks.py'))
    print(f'wrote {DOC}')


def main():
    baseline = measure.load_results(RESULTS) if '--doc' in sys.argv[1:] else measure_c()
    write_doc(baseline)


if __name__ == '__main__':
    main()
