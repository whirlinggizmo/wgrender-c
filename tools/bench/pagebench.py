#!/usr/bin/env python3
"""What a built web page costs, measured in a browser: the benchmark harness's
measurements (measure.py calls these; a binding's tools can too).

    frame(site, label, ...)   script and task time per frame, and the frame interval
    gc(site, label, ...)      JS heap allocation per frame, and the collections V8 traced
    calls(site, label, ...)   wgr calls per frame made by a guest that runs as JS

    tools/bench/pagebench.py frame|gc|calls --site=DIR [--url=PATH] [--probe=FILE] ...
                                             (prints the result as one line of JSON)

Each serves `site` with tools/serve.py, waits for `probe` to be served, loads `url` in
a fresh browser (display 'headless': SwiftShader, a CPU renderer; 'xvfb': the real GPU
on a virtual X display, for a scene heavy enough to draw that software rasterizing
would bound the frame), lets it warm up, and samples. --url picks a page within the
site: wgrender's own example shell serves every example from one index, selected with
?ex=NAME.

frame: the frame interval (requestAnimationFrame deltas) is capped at the display rate,
so a fast example reads ~16.7 ms whatever it does; what separates two builds of the same
example is the work inside the frame, which Chrome's own CPU accounting
(Performance.getMetrics) gives: script and total task time per frame. ScriptDuration is
the telling one: a C build runs almost nothing in JS, while a JS guest's whole game loop
does, plus a marshalling step per wgrender call.

gc: allocation is the cost the frame interval hides hardest. At a vsync-capped 60 fps
there are ~16 ms of slack, so a collection has to be enormous before it is a late
frame; the cost is real long before it is visible, and it grows with how long the
program runs, not with what is on screen. So: performance.memory.usedJSHeapSize once
per frame (into a preallocated Float64Array, so the sampler makes no garbage, with
--enable-precise-memory-info, else Chrome rounds to 100 KB); rising is allocation, a
fall a collection. And what collections cost comes from V8's own accounting: the run
is traced (devtools.timeline) and every MinorGC/MajorGC is reported with its duration.
`load` burns that many ms of arithmetic in the page's rAF callback every frame,
allocating nothing: it stands in for game logic and takes the slack away, identically
for every language. `uncapped` takes the vsync limiter off; then the rAF rate measures
how fast the page issues frames, not how fast it draws them. Only the JS heap: a wasm
module's linear memory isn't in this number, which is the distinction measured; a C
guest reads near zero.

calls: only a guest that runs as JS crosses into the wasm to call wgrender, so only it
has calls to count; the count turns callbench's per-call cost into a per-frame one. The
guest is handed the host by `<guest>.start(host)` (the guest ABI's boot, wgr_guest.h).
Before the page loads, the global the guest defines is trapped and its start wrapped,
so every `_wgr_*` export on the host it is given counts its calls. Nothing in the built
site changes; the wrapper adds a call to each wgr call, so time frames with frame().
"""
import argparse
import contextlib
import json
import sys
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools/: weblib
import weblib  # noqa: E402


@contextlib.contextmanager
def page_on(site, label, probe='wgrender-host.js', display='headless', extra_args=()):
    """(page session, site base URL): a browser on its own profile, the site served."""
    run = weblib.RunProcesses(label)
    try:
        port = weblib.free_port()
        run.spawn([weblib.PYTHON, weblib.ROOT / 'tools' / 'serve.py', port, Path(site).resolve()])
        weblib.wait_for(f'http://127.0.0.1:{port}/{probe}', 'serve.py')
        debug_base, browser = weblib.launch_browser(run, weblib.find_browser(), display, extra_args=extra_args)
        target = browser.send('Target.createTarget', {'url': 'about:blank'})['targetId']
        page = weblib.open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{target}')
        page.send('Runtime.enable')
        page.send('Page.enable')
        yield page, f'http://127.0.0.1:{port}'
        page.close()
        browser.close()
    finally:
        run.stop()


def evaluate(page, expression, timeout=15):
    return page.send('Runtime.evaluate', {'expression': expression, 'returnByValue': True},
                     timeout)['result'].get('value')


def _at(sorted_values, q):
    return sorted_values[min(len(sorted_values) - 1, int(len(sorted_values) * q))]


def frame(site, label='bench', url='/', probe='wgrender-host.js', display='headless', warmup=4000, sample=8000):
    with page_on(site, label, probe, display) as (page, base):
        page.send('Page.navigate', {'url': base + url})
        page.send('Performance.enable')
        time.sleep(warmup / 1000)

        def metrics():
            return {m['name']: m['value'] for m in page.send('Performance.getMetrics')['metrics']}

        before = metrics()
        # start sampling only after warmup: the first seconds are asset loads, shader
        # compiles and JIT, none of which is the steady-state frame cost compared
        evaluate(page, """globalThis.__f = []; (function tick(p) {
            requestAnimationFrame((t) => { if (p) globalThis.__f.push(t - p); tick(t); });
        })(0);""")
        time.sleep(sample / 1000)
        after = metrics()
        frames = sorted(json.loads(evaluate(page, 'JSON.stringify(globalThis.__f)')))
    if len(frames) < 30:
        raise RuntimeError(f'{label}: only {len(frames)} frames: did it start?')

    def per(name):
        return round((after.get(name, 0) - before.get(name, 0)) * 1000 / len(frames), 3)

    return {'label': label, 'frames': len(frames), 'median': round(_at(frames, 0.5), 2),
            'p95': round(_at(frames, 0.95), 2),
            'overCap': round(sum(1 for f in frames if f > 18) / len(frames) * 100, 1),
            'scriptMs': per('ScriptDuration'), 'taskMs': per('TaskDuration')}


def gc(site, label='gcbench', url='/', probe='wgrender-host.js', display='headless', warmup=4000, sample=10000,
       load=0, uncapped=False):
    extra = ['--enable-precise-memory-info', *(['--disable-gpu-vsync', '--disable-frame-rate-limit'] if uncapped else [])]
    with page_on(site, label, probe, display, extra) as (page, base):
        events, done = [], threading.Event()

        def on_event(msg):
            if msg['method'] == 'Tracing.dataCollected':
                events.extend({'name': e['name'], 'us': e.get('dur', 0)} for e in msg['params']['value']
                              if e.get('name') in ('MinorGC', 'MajorGC'))
            elif msg['method'] == 'Tracing.tracingComplete':
                done.set()

        page.on_event(on_event)
        page.send('Page.navigate', {'url': base + url})
        # after warmup: the first seconds are asset loads, shader compiles and JIT, and
        # the garbage those make says nothing about the steady state
        time.sleep(warmup / 1000)
        page.send('Tracing.start', {'traceConfig': {'includedCategories': ['devtools.timeline']}})
        n_max = -(-sample // 16) + 240
        evaluate(page, f"""globalThis.__n = 0;
            globalThis.__dt = new Float64Array({n_max});
            globalThis.__hp = new Float64Array({n_max});
            // The burn: 32-bit integer arithmetic, and the iteration count calibrated
            // once so the loop never reads the clock. Both matter: a float loop boxes
            // its intermediates as heap numbers, and polling performance.now() a few
            // thousand times a frame allocates one per call, either of which would make
            // the harness the biggest allocator in the run and measure itself.
            globalThis.__sink = 1;
            globalThis.__spin = function (n) {{
                let x = globalThis.__sink | 0;
                for (let i = 0; i < n; i++) x = (Math.imul(x, 1664525) + 1013904223) | 0;
                globalThis.__sink = x;
            }};
            globalThis.__iters = 0;
            if ({load} > 0) {{
                globalThis.__spin(5e6); // let it tier up before timing it
                const t0 = performance.now();
                globalThis.__spin(2e7);
                globalThis.__iters = Math.round(2e7 / (performance.now() - t0) * {load});
            }}
            (function tick(p) {{
                requestAnimationFrame((t) => {{
                    if (globalThis.__iters > 0) globalThis.__spin(globalThis.__iters);
                    if (p && globalThis.__n < {n_max}) {{
                        globalThis.__dt[globalThis.__n] = t - p;
                        globalThis.__hp[globalThis.__n] = performance.memory.usedJSHeapSize;
                        globalThis.__n++;
                    }}
                    tick(t);
                }});
            }})(0);""", 60)
        time.sleep(sample / 1000)
        page.send('Tracing.end')
        done.wait(30)
        n = evaluate(page, 'globalThis.__n')
        dt = json.loads(evaluate(page, f'JSON.stringify(Array.from(globalThis.__dt.subarray(0, {n})))'))
        hp = json.loads(evaluate(page, f'JSON.stringify(Array.from(globalThis.__hp.subarray(0, {n})))'))
    if n < 30:
        raise RuntimeError(f'{label}: only {n} frames: did it start?')

    allocated = freed = 0
    collections = []
    for i in range(1, n):
        d = hp[i] - hp[i - 1]
        if d >= 0:
            allocated += d
        else:
            freed -= d
            collections.append(dt[i])
    gc_total_ms = round(sum(e['us'] for e in events) / 1000, 1)

    # Split them: a median over a handful of collections that are half major says
    # something quite different from a median over three hundred nearly all minor.
    def by_kind(name):
        us = sorted(e['us'] for e in events if e['name'] == name)
        if not us:
            return {'count': 0, 'medianMs': None, 'maxMs': None, 'totalMs': 0}
        return {'count': len(us), 'medianMs': round(us[len(us) // 2] / 1000, 2), 'maxMs': round(us[-1] / 1000, 2),
                'totalMs': round(sum(us) / 1000, 1)}

    frames = sorted(dt)
    gc_frames = sorted(collections)
    late = sum(1 for f in dt if f > 20)
    return {
        'label': label, 'frames': n, 'uncapped': uncapped,
        'allocBytesPerFrame': round(allocated / n),
        'allocMBPerMinute': round(allocated / n * 3600 / 1048576, 1),
        'collections': len(collections),
        'freedMB': round(freed / 1048576, 2),
        'heapStartMB': round(hp[0] / 1048576, 1),
        'heapEndMB': round(hp[n - 1] / 1048576, 1),
        'frameMs': {'median': round(_at(frames, 0.5), 2), 'p99': round(_at(frames, 0.99), 2),
                    'max': round(frames[-1], 2)},
        'gcFrameMs': ({'median': round(gc_frames[len(gc_frames) // 2], 2), 'max': round(gc_frames[-1], 2)}
                      if gc_frames else None),
        # V8's own accounting, which does not care whether the frame had slack to hide in
        'loadMs': load,
        'lateFrames': {'over20': late, 'over33': sum(1 for f in dt if f > 33), 'pct': round(late / n * 100, 2)},
        'gc': {'totalMs': gc_total_ms, 'pctOfWall': round(gc_total_ms / sample * 100, 2),
               'minor': by_kind('MinorGC'), 'major': by_kind('MajorGC')},
        'bufferFull': n >= n_max,
    }


CALLS_HOOK = """(() => {
    const calls = globalThis.__wgrCalls = {};
    globalThis.__wgrFrames = 0;
    (function tick() { globalThis.__wgrFrames++; requestAnimationFrame(tick); })();
    let value;
    Object.defineProperty(globalThis, GUEST, {
        configurable: true,
        get: () => value,
        set: (g) => {
            const start = g.start;
            g.start = (host, ...rest) => {
                for (const k of Object.keys(host)) {
                    if (!k.startsWith("_wgr_") || typeof host[k] !== "function") continue;
                    const f = host[k];
                    calls[k] = 0;
                    host[k] = (...a) => { calls[k]++; return f(...a); };
                }
                return start.call(g, host, ...rest);
            };
            value = g;
        },
    });
})();"""


def calls(site, label='callcount', url='/', probe='wgrender-host.js', display='headless', guest='WgrGuest',
          warmup=4000, sample=8000):
    with page_on(site, label, probe, display) as (page, base):
        page.send('Page.addScriptToEvaluateOnNewDocument', {'source': CALLS_HOOK.replace('GUEST', json.dumps(guest))})
        page.send('Page.navigate', {'url': base + url})

        def read():
            return json.loads(evaluate(page, 'JSON.stringify({ calls: globalThis.__wgrCalls, '
                                             'frames: globalThis.__wgrFrames })'))

        time.sleep(warmup / 1000)
        before = read()
        time.sleep(sample / 1000)
        after = read()
    frames = after['frames'] - before['frames']
    if not after['calls']:
        raise RuntimeError(f'{label}: no _wgr_ calls seen: is {guest}.start how this page boots?')
    per_frame = {}
    for k, n in after['calls'].items():
        d = n - before['calls'].get(k, 0)
        if d:
            per_frame[k[1:]] = round(d / frames, 2)
    return {'label': label, 'frames': frames, 'callsPerFrame': round(sum(per_frame.values()), 2),
            'perFrame': per_frame}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', choices=['frame', 'gc', 'calls'])
    ap.add_argument('--site', default='out/web')
    ap.add_argument('--label')
    ap.add_argument('--url', default='/')
    ap.add_argument('--probe', default='wgrender-host.js')
    ap.add_argument('--display', default='headless', choices=['headless', 'xvfb'])
    ap.add_argument('--warmup', type=int, default=4000)
    ap.add_argument('--sample', type=int)
    ap.add_argument('--load', type=int, default=0, help='gc: ms burned in every frame')
    ap.add_argument('--uncapped', action='store_true', help='gc: vsync off')
    ap.add_argument('--guest', default='WgrGuest', help='calls: the global the guest defines')
    a = ap.parse_args()
    common = dict(url=a.url, probe=a.probe, display=a.display, warmup=a.warmup)
    if a.what == 'frame':
        result = frame(a.site, a.label or 'bench', sample=a.sample or 8000, **common)
    elif a.what == 'gc':
        result = gc(a.site, a.label or 'gcbench', sample=a.sample or 10000, load=a.load, uncapped=a.uncapped, **common)
    else:
        result = calls(a.site, a.label or 'callcount', guest=a.guest, sample=a.sample or 8000, **common)
    print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as e:
        sys.exit(f'pagebench: {e}')
