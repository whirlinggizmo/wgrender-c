"""The cross-binding benchmark harness, shared by wgrender and every binding.

wgrender's tools/benchmarks.py measures the C baseline with it; a binding's own
tools/benchmarks.py imports it from its wgrender checkout (the submodule, or
WGRENDER_DIR) and measures itself the same way:

    sys.path.insert(0, str(wgrender / 'tools/bench'))
    import measure

Each project writes what it measured to bench/results.json (write_results) and renders
docs/benchmarks.md from it (render_doc), so the numbers of every binding are taken
by the same code and printed by the same code. Nothing here builds a project: the
caller builds, then hands over the site directory and its files.

A configuration is one way of shipping the same example (C, Nim -> C, Haxe -> JS
guest, ...). Its entry in results.json:

    {"id", "label", "project", "example", "toolchain",
     "sizes":  {"files": [{"name", "kind", "raw", "gzip", "brotli"}], "total": {...}},
               kind: wasm, js, or page (the HTML and anything else it fetches)
     "frame":  bench.mjs's output       (script and task ms per frame)
     "gc":     gcbench.mjs's output     (JS heap allocation and V8 collections)
     "calls":  callcount.mjs's output   (JS guests only: wgr calls per frame)
     "stress": [{"n", "frame", "gc"}]   the stress scene (tools/bench/stress.c) at each
                                        entity count, where the configuration has it}

Numbers are only comparable when taken on the same machine against the same
wgrender, so every results.json records both, and render_doc flags a row that
differs from the baseline in either.
"""
import datetime
import gzip
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
WGRENDER = HERE.parents[1]
SCHEMA = 1

# Every configuration is measured on this: webgl2 without threads is what a plain
# static host can serve, and the only web build hxcpp can link against.
WEB_VARS = {'BACKEND': 'webgl2', 'WEB_THREADS': '0'}


def run(cmd, cwd=None, env=None, capture=False):
    print('  $ ' + ' '.join(str(c) for c in cmd), flush=True)
    merged = dict(os.environ, **(env or {}))
    if capture:
        done = subprocess.run([str(c) for c in cmd], cwd=cwd, env=merged, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if done.returncode != 0:
            sys.exit(f'failed ({done.returncode}): {" ".join(str(c) for c in cmd)}\n{done.stderr}')
        return done.stdout
    if subprocess.run([str(c) for c in cmd], cwd=cwd, env=merged).returncode != 0:
        sys.exit(f'failed: {" ".join(str(c) for c in cmd)}')


# --- sizes -------------------------------------------------------------------

def _brotli(data):
    try:
        import brotli
        return len(brotli.compress(data, quality=11))
    except ImportError:
        pass
    if not shutil.which('brotli'):
        sys.exit('measure: brotli is needed (pip install brotli, or the brotli tool)')
    return len(subprocess.run(['brotli', '-c', '-q', '11'], input=data, capture_output=True,
                              check=True).stdout)


KINDS = {'.wasm': 'wasm', '.js': 'js', '.mjs': 'js'}


def sizes(files):
    """Raw, gzip -9 and brotli q11 of each file, and their sums: each file compressed
    on its own, the way a browser fetches them. Pass everything the page loads before
    the first frame, the page itself included, so the total is the first visit."""
    rows = []
    for path in files:
        path = pathlib.Path(path)
        if not path.is_file():
            sys.exit(f'measure: {path} is not built')
        data = path.read_bytes()
        rows.append({'name': path.name, 'kind': KINDS.get(path.suffix, 'page'),
                     'raw': len(data), 'gzip': len(gzip.compress(data, 9)), 'brotli': _brotli(data)})
    total = {k: sum(r[k] for r in rows) for k in ('raw', 'gzip', 'brotli')}
    return {'files': rows, 'total': total}


# --- the browser -------------------------------------------------------------

def _node(script, args):
    out = run(['node', HERE / script] + args, capture=True).strip().splitlines()
    return json.loads(out[-1])


def _page_args(site, label, url, probe, display='headless'):
    return [f'--site={site}', f'--label={label}', f'--url={url}', f'--probe={probe}', f'--display={display}']


FRAME_RUNS = 3


def frame(site, label, url='/', probe='wgrender-host.js', display='headless'):
    """bench.mjs: script and task time per frame, from Chrome's CPU accounting. The run
    with the median script time of FRAME_RUNS: one run alone moves by a tenth of a
    millisecond, which is more than the differences being measured."""
    runs = [_node('bench.mjs', _page_args(site, label, url, probe, display)) for _ in range(FRAME_RUNS)]
    runs.sort(key=lambda r: r['scriptMs'])
    return dict(runs[len(runs) // 2], scriptRuns=[r['scriptMs'] for r in runs])


def gc(site, label, url='/', probe='wgrender-host.js', display='headless', load=0):
    """gcbench.mjs: JS heap allocation per frame and the collections V8 traced. `load`
    burns that many ms in each frame first, taking the slack a pause would hide in."""
    return _node('gcbench.mjs', _page_args(site, label, url, probe, display) + [f'--load={load}'])


def calls(site, label, url='/', probe='wgrender-host.js', guest='WgrGuest'):
    """callcount.mjs: wgr calls per frame, for a guest that runs as JS."""
    return _node('callcount.mjs', _page_args(site, label, url, probe) + [f'--guest={guest}'])


# The stress scene (tools/bench/stress.c) at these entity counts. It draws thousands of
# blended sprites, which headless Chrome rasterizes in software (SwiftShader) slowly
# enough to bound the frame, hiding the language cost being measured, so it runs on the
# real GPU: a virtual X display (Xvfb) with ANGLE on Vulkan. The GC runs burn
# STRESS_LOAD_MS a frame on top, so a pause of a few ms is a late frame, not slack.
STRESS_COUNTS = (1000, 5000, 10000)
STRESS_DISPLAY = 'xvfb'
STRESS_LOAD_MS = 8


def stress(site, label, url, probe='wgrender-host.js'):
    """The stress scene at each of STRESS_COUNTS. `url` has an {n} for the count."""
    if not shutil.which('Xvfb'):
        sys.exit('measure: the stress scene needs Xvfb and a GPU (see STRESS_DISPLAY)')
    rows = []
    for n in STRESS_COUNTS:
        page = {'url': url.format(n=n), 'probe': probe, 'display': STRESS_DISPLAY}
        rows.append({'n': n, 'frame': frame(site, f'{label}-{n}', **page),
                     'gc': gc(site, f'{label}-{n}', load=STRESS_LOAD_MS, **page)})
    return rows


def callbench():
    """The per-call cost of JS -> wasm against the same call inside the wasm."""
    out = WGRENDER / 'build/callbench'
    out.mkdir(parents=True, exist_ok=True)
    src = HERE / 'callbench'
    run(['emcc', '-O2', src / 'shapes.c', src / 'loops.c', '-o', out / 'callbench.js',
         '-sMODULARIZE', '-sEXPORT_ES6', '-sENVIRONMENT=node',
         '-sEXPORTED_RUNTIME_METHODS=stackAlloc,stackSave,stackRestore,lengthBytesUTF8,stringToUTF8,HEAP32'])
    return json.loads(run(['node', src / 'run.mjs', out / 'callbench.js'], capture=True).strip().splitlines()[-1])


# --- where and against what --------------------------------------------------

def _first_line(cmd):
    try:
        done = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        return None
    text = (done.stdout or done.stderr).strip()
    return text.splitlines()[0] if text else None


def _cpu():
    try:
        for line in pathlib.Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith('model name'):
                return line.split(':', 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def _browser():
    path = _first_line(['node', '-e',
                        'import("' + (HERE.parent / 'weblib.mjs').as_uri() + '").then('
                        '(m) => console.log(m.findBrowser(process.env.WEBCHECK_BROWSER)))'])
    return _first_line([path, '--version']) if path else None


def environment():
    """The machine and tools a run was taken with. Two results are comparable when
    `machine` matches; the rest is there to explain a difference."""
    emcc = _first_line(['emcc', '--version']) or ''
    return {
        'date': datetime.date.today().isoformat(),
        'machine': f'{_cpu()}, {platform.system()}',
        'browser': _browser(),
        'node': _first_line(['node', '--version']),
        'emcc': emcc.split(')')[1].split()[0] if ')' in emcc else emcc,
    }


# What cannot change a number: what a benchmark run writes into wgrender itself, the
# prose, and the script that only runs the others in order. A commit that only touches
# these is not a different wgrender; without this, committing the baseline's results,
# or a README line, would make every binding pinned before it read as measured
# against another one.
NOT_WGRENDER = [':(exclude)bench', ':(exclude)docs', ':(exclude,glob)**/*.md',
                ':(exclude)tools/benchmarks.py']


def wgrender_info(wgr_dir, source):
    """The wgrender a project was built against: the last commit that changed anything
    that can change a number (NOT_WGRENDER), and whether there were uncommitted changes
    to any of it.
    `source` says where it came from: 'self', 'submodule', 'WGRENDER_DIR', ..."""
    git = ['git', '-C', str(wgr_dir)]
    commit = _first_line(git + ['log', '-1', '--format=%h', '--', '.'] + NOT_WGRENDER)
    dirty = bool(subprocess.run(git + ['status', '--porcelain', '--untracked-files=no', '--', '.']
                                + NOT_WGRENDER, capture_output=True, text=True).stdout.strip())
    return {'commit': commit, 'dirty': dirty, 'source': source}


def find_wgrender(project_root):
    """wgrender for a binding: WGRENDER_DIR when set, else the submodule."""
    if os.environ.get('WGRENDER_DIR'):
        return pathlib.Path(os.environ['WGRENDER_DIR']).resolve(), 'WGRENDER_DIR'
    sub = pathlib.Path(project_root) / 'project/lib/wgrender-c'
    if not (sub / 'include').is_dir():
        sys.exit(f'measure: no wgrender at {sub} (git submodule update --init, or set WGRENDER_DIR)')
    return sub.resolve(), 'submodule'


# --- results.json ------------------------------------------------------------

def write_results(path, project, wgrender, configurations, extra=None):
    data = {'schema': SCHEMA, 'project': project, 'environment': environment(),
            'wgrender': wgrender, 'configurations': configurations}
    data.update(extra or {})
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n')
    print(f'wrote {path}')
    return data


def load_results(path):
    data = json.loads(pathlib.Path(path).read_text())
    if data.get('schema') != SCHEMA:
        sys.exit(f'{path}: results schema {data.get("schema")}, this harness reads {SCHEMA}')
    for key in ('project', 'environment', 'wgrender', 'configurations'):
        if key not in data:
            sys.exit(f'{path}: no "{key}"')
    return data


# --- docs/benchmarks.md ------------------------------------------------------

def read_notes(root):
    """A project's bench/notes.md, the hand-written part of its docs/benchmarks.md."""
    path = pathlib.Path(root) / 'bench/notes.md'
    return path.read_text() if path.is_file() else None


def _n(v):
    return f'{v:,}'


def _ms(v):
    return '-' if v is None else f'{v:.2f}'


def _gc_pauses(g):
    parts = []
    for kind in ('minor', 'major'):
        c = g['gc'][kind]
        if c['count']:
            parts.append(f"{c['count']} {kind}, {c['totalMs']:.1f} ms")
    return ', '.join(parts) or 'none'


def render_doc(title, lead, results, baseline, generator, notes=None):
    """docs/benchmarks.md from a list of results files. `baseline` is wgrender's own
    results; its C configuration is the 1.00x every size is set against, and a result
    taken on another machine or against another wgrender is flagged, not dropped.
    `notes` is hand-written markdown (a project's bench/notes.md, see read_notes),
    placed under its own heading: what the tables mean and what they leave out, which
    a regenerated file would otherwise lose."""
    base_env, base_wgr = baseline['environment'], baseline['wgrender']
    base_c = next(c for c in baseline['configurations'] if c['id'] == 'c')
    rows = []  # (configuration, flags)
    for r in results:
        flags = []
        if r['environment']['machine'] != base_env['machine']:
            flags.append(f"other machine: {r['environment']['machine']}")
        if r['wgrender']['commit'] != base_wgr['commit']:
            flags.append(f"wgrender {r['wgrender']['commit']}, baseline {base_wgr['commit']}")
        if r['wgrender'].get('dirty'):
            flags.append('wgrender had uncommitted changes')
        for c in r['configurations']:
            rows.append((r, c, flags))

    def mark(flags):
        return ' *' if flags else ''

    out = [f'# {title}', '',
           f'Generated by `{generator}`: do not edit. Baseline measured {base_env["date"]} on '
           f'{base_env["machine"]}, {base_env["browser"]}, Emscripten {base_env["emcc"]}, '
           f'against wgrender `{base_wgr["commit"]}`.', '', lead, '']

    out += ['## Download size', '',
            'Everything a first visit downloads before the first frame, assets aside: the '
            'wasm, every JS file and the page. Each file compressed on its own, as a browser '
            f'fetches it, and summed. `{base_c["example"]}` in every configuration, webgl2, '
            'no threads, release. wasm, JS and page are raw bytes.', '',
            '| Configuration | wasm | JS | page | total raw | total gzip | total brotli | vs C (brotli) |',
            '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    cb = base_c['sizes']['total']['brotli']
    for r, c, flags in sorted(rows, key=lambda x: x[1]['sizes']['total']['brotli']):
        s = c['sizes']
        raw = {k: sum(f['raw'] for f in s['files'] if f['kind'] == k) for k in ('wasm', 'js', 'page')}
        t = s['total']
        out.append(f"| {c['label']}{mark(flags)} | {_n(raw['wasm'])} | {_n(raw['js'])} | "
                   f"{_n(raw['page'])} | {_n(t['raw'])} | "
                   f"{_n(t['gzip'])} | {_n(t['brotli'])} | {t['brotli'] / cb:.2f}x |")

    out += ['', '## Frame cost', '',
            'Chrome\'s own CPU accounting over 8 s of steady state (`tools/bench/bench.mjs`), '
            'the median of three runs: the frame interval is capped at the display rate and '
            'hides the work inside it.', '',
            '| Configuration | script (ms/frame) | task (ms/frame) | script, all runs |',
            '| --- | ---: | ---: | --- |']
    for r, c, flags in sorted(rows, key=lambda x: x[1]['frame']['scriptMs']):
        f = c['frame']
        spread = ', '.join(_ms(v) for v in f.get('scriptRuns', [f['scriptMs']]))
        out.append(f"| {c['label']}{mark(flags)} | {_ms(f['scriptMs'])} | {_ms(f['taskMs'])} | {spread} |")

    # A configuration whose game code runs as JS is the one with calls counted at the
    # host's exports; the rest run inside the wasm and allocate nothing on the JS heap
    # themselves, so their readings are the page's noise floor, not a ranking.
    in_js = [x for x in rows if x[1].get('calls')]
    in_wasm = [x for x in rows if not x[1].get('calls')]
    out += ['', '## JS heap and GC', '',
            'V8\'s traced collections over 10 s at 60 fps (`tools/bench/gcbench.mjs`). Only the '
            'JS heap: a collector inside the wasm (hxcpp\'s) is not visible here at all.', '',
            '| Configuration | game code runs in | alloc (B/frame) | alloc (MB/min) | collections traced | late frames |',
            '| --- | --- | ---: | ---: | --- | ---: |']
    for group, where in ((sorted(in_js, key=lambda x: -x[1]['gc']['allocBytesPerFrame']), 'JS'),
                         (sorted(in_wasm, key=lambda x: x[1]['sizes']['total']['brotli']), 'wasm')):
        for r, c, flags in group:
            g = c['gc']
            out.append(f"| {c['label']}{mark(flags)} | {where} | {_n(g['allocBytesPerFrame'])} | "
                       f"{g['allocMBPerMinute']} | {_gc_pauses(g)} | {g['lateFrames']['over20']} |")
    if in_wasm:
        floor = [x[1]['gc']['allocBytesPerFrame'] for x in in_wasm]
        out += ['', f'Code running in the wasm allocates nothing on the JS heap itself, so those rows '
                    f'({_n(min(floor))} to {_n(max(floor))} B/frame here) are the page\'s own noise: '
                    'Emscripten\'s glue, the page and the measuring. Their order means nothing.']

    stressed = [(r, c, flags) for r, c, flags in rows if c.get('stress')]
    if stressed:
        counts = sorted({s['n'] for _, c, _ in stressed for s in c['stress']})
        out += ['', '## Stress', '',
                'Game logic at scale (`tools/bench/stress.c`, ported line for line): N entities '
                'moved every frame, one wgr call each, about N/240 of them replaced a frame '
                '(objects destroyed and created; in Nim, Beef and Haxe, allocated and freed or '
                'collected), and 49 lines of formatted text. On the real GPU (Xvfb, ANGLE on '
                'Vulkan): headless Chrome rasterizes this many sprites in software, slowly '
                'enough to bound the frame.', '',
                'Script time per frame, the median of three runs:', '',
                '| Configuration | ' + ' | '.join(f'n = {_n(k)}' for k in counts) + ' |',
                '| --- |' + ' ---: |' * len(counts)]
        for r, c, flags in stressed:
            by = {s['n']: s for s in c['stress']}
            out.append(f"| {c['label']}{mark(flags)} | "
                       + ' | '.join(f"{_ms(by[k]['frame']['scriptMs'])}" if k in by else '' for k in counts) + ' |')
        load = next(s['gc'].get('loadMs', 0) for _, c, _ in stressed for s in c['stress'])
        out += ['', f'With {load:g} ms of other work burned in every frame, so a pause has little '
                    'slack to hide in: JS heap allocation, the collections V8 traced, and frames '
                    'over 20 ms (`tools/bench/gcbench.mjs`, 10 s).', '',
                '| Configuration | n | alloc (MB/min) | collections traced | worst frame (ms) | late frames |',
                '| --- | ---: | ---: | --- | ---: | ---: |']
        for r, c, flags in stressed:
            for s_ in sorted(c['stress'], key=lambda s_: s_['n']):
                g_ = s_['gc']
                out.append(f"| {c['label']}{mark(flags)} | {_n(s_['n'])} | {g_['allocMBPerMinute']} | "
                           f"{_gc_pauses(g_)} | {_ms(g_['frameMs']['max'])} | {g_['lateFrames']['over20']} |")

    bench = baseline.get('callbench')
    if bench:
        shapes = bench['shapes']
        out += ['', '## Calls from a JS guest', '',
                'What a call from JS into wgrender\'s wasm costs, against the same call made '
                f'inside the wasm (`tools/bench/callbench`, {bench["engine"]}, median of '
                f'{bench["reps"]} runs of {_n(bench["calls"])} calls). The JS side marshals as '
                'a JS guest binding does: struct results read into a new object, strings copied '
                'in with stringToUTF8.', '',
                '| Shape | like | JS -> wasm (ns) | inside wasm (ns) | boundary (ns) | calls per ms of JS |',
                '| --- | --- | ---: | ---: | ---: | ---: |']
        for name, sh in shapes.items():
            out.append(f"| {name} | `{sh['c']}` | {sh['jsNs']:.2f} | {sh['wasmNs']:.2f} | "
                       f"{sh['boundaryNs']:.2f} | {_n(int(round(1e6 / sh['jsNs'], -3)))} |")
        if in_js:
            worst = max(sh['jsNs'] for sh in shapes.values())
            names = sorted({k for _, c, _ in in_js for k in c['calls']['perFrame']})
            out += ['', 'wgr calls per frame, counted at the host\'s exports '
                        '(`tools/bench/callcount.mjs`):', '',
                    '| Call | ' + ' | '.join(c['label'] + mark(f) for _, c, f in in_js) + ' |',
                    '| --- |' + ' ---: |' * len(in_js)]
            for k in sorted(names, key=lambda k: (-max(c['calls']['perFrame'].get(k, 0) for _, c, _ in in_js), k)):
                out.append(f'| `{k}` | ' + ' | '.join(f"{c['calls']['perFrame'].get(k, 0):g}"
                                                     for _, c, _ in in_js) + ' |')
            out.append('| **total** | ' + ' | '.join(f"**{c['calls']['callsPerFrame']:g}**" for _, c, _ in in_js) + ' |')
            for _, c, _ in in_js:
                n = c['calls']['callsPerFrame']
                out += ['', f"{c['label']}: {n:g} calls a frame cost at most {n * worst / 1000:.2f} µs "
                            f"even if every one were the dearest shape above ({worst:.0f} ns), against "
                            "a 16.7 ms frame."]

    flagged = {id(r): (r, flags) for r, c, flags in rows if flags}
    if flagged:
        out += ['', '\\* Not comparable as-is:', '']
        for r, flags in flagged.values():
            out.append(f"- {r['project']} ({r['environment']['date']}): {'; '.join(flags)}")

    if notes:
        out += ['', '## Notes', '', notes.strip()]

    out += ['', '## Sources', '',
            '| Project | measured | against wgrender | toolchains |', '| --- | --- | --- | --- |']
    for r in results:
        tools = ', '.join(dict.fromkeys(part for c in r['configurations'] if c.get('toolchain')
                                        for part in c['toolchain'].split(', ')))
        out.append(f"| {r['project']} | {r['environment']['date']} | `{r['wgrender']['commit']}` "
                   f"({r['wgrender']['source']}) | {tools} |")
    return '\n'.join(out) + '\n'
