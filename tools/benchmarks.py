#!/usr/bin/env python3
"""wgrender's benchmarks: the C baseline, and every binding's numbers beside it.

    tools/benchmarks.py          build the web examples, measure the C `simple`,
                                 write bench/results.json and docs/benchmarks.md
    tools/benchmarks.py --doc    only regenerate docs/benchmarks.md from the results
                                 files already there
    tools/benchmarks.py --all    the whole refresh: the C baseline, then every
                                 binding's own tools/benchmarks.py, then this page

Run by hand, not in CI: it drives a browser for about a minute (--all: several).
Commit bench/results.json and docs/benchmarks.md afterwards, and with --all each
binding's too. bench/notes.md is the
hand-written part of the page (what the numbers mean, what is not measured); edit
it, then --doc.

A binding measures itself with its own tools/benchmarks.py, which uses this
repository's tools/bench/ (measure.py) and writes the binding's bench/results.json.
The bindings live under bindings/ (BINDINGS); the doc reads each one's results and
lists any that has none. It builds none of them: --all only runs each one's own
script, in the order the numbers need (the baseline first, since a binding's page
compares against it). A binding measured on another machine or against another
wgrender commit is flagged in the doc rather than left out.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/bench'))
sys.path.insert(0, str(ROOT / 'tools'))
import builds  # noqa: E402
import measure  # noqa: E402

RESULTS = ROOT / 'bench/results.json'
DOC = ROOT / 'docs/benchmarks.md'
BINDINGS = {'wgrender-hx': ROOT / 'bindings/haxe'}
EXAMPLE = 'simple'


def measure_c():
    # the preset measure.WEB_VARS names: the site (every example, the page and its
    # examples.json) and the stress page under bench/
    preset = 'wasm32-release'
    measure.run(['cmake', '--preset', preset], cwd=ROOT)
    measure.run(['cmake', '--build', '--preset', preset], cwd=ROOT)
    measure.run(['cmake', '--build', '--preset', preset, '--target', 'stress'], cwd=ROOT)
    site = builds.programs(preset)
    page = {'url': f'/?ex={EXAMPLE}', 'probe': f'{EXAMPLE}.js'}
    c = {
        'id': 'c', 'label': 'C', 'project': 'wgrender-c', 'example': EXAMPLE,
        'toolchain': f'Emscripten {measure.environment()["emcc"]}',
        # the page is wgrender's example shell, which fetches examples.json for its picker
        'sizes': measure.sizes([site / f'{EXAMPLE}.wasm', site / f'{EXAMPLE}.js',
                                site / 'index.html', site / 'examples.json']),
        'frame': measure.frame(site, 'c', **page),
        'gc': measure.gc(site, 'c', **page),
        'stress': measure.stress(site / 'bench', 'c', '/?ex=stress&n={n}', 'stress.js'),
    }
    return measure.write_results(RESULTS, 'wgrender-c', measure.wgrender_info(ROOT, 'self'), [c],
                                 {'callbench': measure.callbench()})


def write_doc(baseline):
    results, missing = [baseline], []
    for name, where in BINDINGS.items():
        path = where / 'bench/results.json'
        if path.is_file():
            results.append(measure.load_results(path))
        else:
            missing.append(name)
    lead = ('Every binding against the C, on the same example. Each project measures itself '
            '(its `tools/benchmarks.py`) with the harness in `tools/bench/`, and this page '
            'collects what they recorded.')
    if missing:
        lead += ' Not collected, no results found: ' + ', '.join(missing) + '.'
    DOC.write_text(measure.render_doc('wgrender benchmarks', lead, results, baseline,
                                      'tools/benchmarks.py', measure.read_notes(ROOT)))
    print(f'wrote {DOC}')


def measure_bindings():
    """Each binding's own tools/benchmarks.py, in turn. One that fails is reported
    and the rest still run; the page then collects whatever results exist."""
    failed = []
    for name, where in BINDINGS.items():
        script = where / 'tools/benchmarks.py'
        print(f'== {name}', flush=True)
        if subprocess.run([sys.executable, script], cwd=script.parents[1]).returncode != 0:
            failed.append(name)
    return failed


def main():
    args = sys.argv[1:]
    if args not in ([], ['--doc'], ['--all']):
        sys.exit(__doc__)
    baseline = measure.load_results(RESULTS) if args == ['--doc'] else measure_c()
    failed = measure_bindings() if args == ['--all'] else []
    write_doc(baseline)
    if failed:
        sys.exit('failed, collected from their last results instead: ' + ', '.join(failed))


if __name__ == '__main__':
    main()
