#!/usr/bin/env python3
"""Web startup: how long each example takes to start, the first time and after.

    tools/webstart.py [options] [example ...]     (after building the web preset)

Each example is opened three times in a fresh browser profile, served the way a
typical host serves it (tools/serve.py --cache --gzip: files kept and revalidated,
compressed):
  cold   nothing cached: everything downloads and compiles
  warm   the second visit: code from the HTTP cache (revalidated), assets from
         libwgrender's IndexedDB file cache
  hot    the third: Chrome keeps compiled wasm from the second visit on (its code
         cache), so this is the best a returning visit gets
and each is timed from navigation to:
  js, wasm     the example's JS glue and wasm downloaded (with the bytes transferred)
  compiled     the wasm compiled and instantiated (WebAssembly.instantiate*); it
               streams, so it overlaps the download
  init         libwgrender's init (worker threads started, main() run, the graphics
               device made, the first animation frame): "wgr:init"
  libwgrender  libwgrender's subsystems set up (shaders, pipelines, pools): "wgr:subsystems"
  user         the program's init callback done: "wgr:user-init"
  fs           the IndexedDB file cache opened (its list of files): "wgr:fs-ready"
  frame        the first frame drawn: "wgr:first-frame"
  ready        the first frame with no asset loads pending
(the wgr:* points are performance marks libwgrender makes in web builds).

Options:
  --backend=webgl2|webgpu   (default webgl2); the site is out/web/<backend>
  --threads=0               the -nothreads build
  --net=none|4g|both        network: the local machine as is, emulated 4G (9 Mbit/s
                            down, 150 ms round trips), or both (default both)
  --headless                WebGL2 on SwiftShader (a CPU renderer) instead of the GPU
                            on a virtual X display; its compile times aren't a GPU's
  --browser=PATH            (or WEBCHECK_BROWSER)
  --json=FILE               also write the numbers as JSON

Another device (a phone): attach to its browser instead of starting one, and serve the
site where it can reach it:
  --devtools=PORT           the browser's DevTools on this machine (Android:
                            adb forward tcp:PORT localabstract:chrome_devtools_remote)
  --url=URL                 the site's address as that device reaches it, e.g.
                            https://192.168.1.200:8443 (served here on that port)
  --tls=CERT,KEY            serve HTTPS (threaded builds need a secure page there)
Its profile isn't fresh, so a cold visit clears the site's cache and storage first;
the GPU driver's own shader cache stays.
"""
import argparse
import json
import socket
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import builds  # noqa: E402
from weblib import (PYTHON, ROOT, RunProcesses, find_browser, find_xvfb, free_port, launch_browser,  # noqa: E402
                    open_session, wait_for)

NETS = {
    'none': None,
    '4g': {'offline': False, 'latency': 150, 'downloadThroughput': 9e6 / 8, 'uploadThroughput': 1.5e6 / 8},
}
VISITS = ['cold', 'warm', 'hot']
WARM_UP = {
    'webgl2': "const g=document.getElementById('c').getContext('webgl2');g.clearColor(1,0,0,1);g.clear(g.COLOR_BUFFER_BIT);",
    'webgpu': "navigator.gpu.requestAdapter().then((a)=>a.requestDevice());",
}
READY_TIMEOUT = 60

# Runs in the page before its own scripts: marks when the wasm is being compiled and
# instantiated ("wasm:start", "wasm:ready"), and the first frame after which no asset
# load is pending ("wgr:ready").
PAGE_PROBE = """(() => {
    for (const name of ["instantiateStreaming", "instantiate"]) {
        const original = WebAssembly[name];
        WebAssembly[name] = function (...args) {
            if (performance.getEntriesByName("wasm:start").length === 0) performance.mark("wasm:start");
            return original.apply(this, args).then((result) => {
                if (performance.getEntriesByName("wasm:ready").length === 0) performance.mark("wasm:ready");
                return result;
            });
        };
    }
    const timer = setInterval(() => {
        if (performance.getEntriesByName("wgr:first-frame").length === 0) return;
        const m = globalThis.Module;
        const pending = m && m._wgri_asset_pending_count ? m._wgri_asset_pending_count() : -1;
        if (pending === 0) { performance.mark("wgr:ready"); clearInterval(timer); }
    }, 5);
})();"""

TIMINGS = """(() => {
    if (!location.search.includes("ex=")) return null;
    const at = (name) => { const e = performance.getEntriesByName(name); return e.length ? e[0].startTime : null; };
    const file = (suffix) => {
        const e = performance.getEntriesByType("resource").find((r) => new URL(r.name).pathname.endsWith(suffix));
        return e ? { end: e.responseEnd, transferred: e.transferSize, size: e.decodedBodySize } : null;
    };
    const name = new URLSearchParams(location.search).get("ex");
    return { compile: at("wasm:start"), compiled: at("wasm:ready"), ready: at("wgr:ready"), init: at("wgr:init"),
             subsystems: at("wgr:subsystems"), user: at("wgr:user-init"), fs: at("wgr:fs-ready"),
             frame: at("wgr:first-frame"), js: file("/" + name + ".js"), wasm: file("/" + name + ".wasm") };
})()"""


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--backend', default='webgl2', choices=['webgl2', 'webgpu'])
    ap.add_argument('--threads', default='1', choices=['0', '1'])
    ap.add_argument('--net', default='both', choices=['none', '4g', 'both'])
    ap.add_argument('--headless', action='store_true')
    ap.add_argument('--browser')
    ap.add_argument('--json')
    ap.add_argument('--devtools', type=int)
    ap.add_argument('--url')
    ap.add_argument('--tls')
    ap.add_argument('examples', nargs='*')
    opts = ap.parse_args()
    opts.nets = ['none', '4g'] if opts.net == 'both' else [opts.net]
    if opts.headless and opts.backend == 'webgpu':
        ap.error('--headless has no WebGPU')
    if (opts.devtools is None) != (opts.url is None):
        ap.error('--devtools and --url go together')
    opts.url = opts.url.rstrip('/') if opts.url else None
    opts.tls = opts.tls.split(',') if opts.tls else None
    opts.display = ('remote' if opts.devtools is not None else 'headless' if opts.headless
                    else 'xvfb' if find_xvfb() else 'screen')
    opts.site = builds.out(builds.web(opts.backend, opts.threads == '1'))
    return opts


def wait_for_port(port):
    for _ in range(100):
        with socket.socket() as s:
            if s.connect_ex(('127.0.0.1', port)) == 0:
                return
        time.sleep(0.1)
    raise RuntimeError(f'tools/serve.py did not start (port {port})')


def visit(session, url):
    """One visit: navigate, wait until ready (or the timeout), read the page's timings."""
    session.send('Page.navigate', {'url': url})
    deadline = time.monotonic() + READY_TIMEOUT
    timings = None
    while time.monotonic() < deadline:
        time.sleep(0.1)
        timings = session.send('Runtime.evaluate', {'returnByValue': True, 'expression': TIMINGS})['result'].get('value')
        if timings and timings.get('ready') is not None:
            break
    return timings


def measure(run, browser_path, base_url, example, net, opts):
    remote = opts.display == 'remote'
    browser = None
    if remote:
        debug_base = f'http://127.0.0.1:{opts.devtools}'
    else:
        debug_base, browser = launch_browser(run, browser_path, opts.display,
                                             profile=str(Path(run.profile) / f'{example}-{net}'))
    try:
        with urllib.request.urlopen(f'{debug_base}/json/list', timeout=10) as r:
            targets = json.loads(r.read())
        page = next((t for t in targets if t['type'] == 'page'), None)  # the tab in front
        if page is None:
            raise RuntimeError(f'no page to use at {debug_base}')
        session = open_session(page['webSocketDebuggerUrl'])
        session.send('Page.enable')
        session.send('Network.enable')
        if remote:  # not a fresh profile: forget the site
            session.send('Network.clearBrowserCache', {}, 60)  # slow on a phone
            origin = f'{urllib.parse.urlsplit(base_url).scheme}://{urllib.parse.urlsplit(base_url).netloc}'
            session.send('Storage.clearDataForOrigin', {'origin': origin, 'storageTypes': 'all'})
        session.send('Network.emulateNetworkConditions',
                     NETS[net] or {'offline': False, 'latency': 0, 'downloadThroughput': -1, 'uploadThroughput': -1})
        # a visitor's browser is already running: start its GPU process and graphics
        # device first, on an unrelated page, so the cold visit doesn't pay for that
        session.send('Page.navigate', {'url': f'data:text/html,<canvas id=c></canvas><script>{WARM_UP[opts.backend]}</script>'})
        time.sleep(2)
        probe = session.send('Page.addScriptToEvaluateOnNewDocument', {'source': PAGE_PROBE})
        visits = {}
        for name in VISITS:
            visits[name] = visit(session, f'{base_url}/?ex={urllib.parse.quote(example)}')
            # let the browser finish writing: libwgrender's file cache (IndexedDB) and the
            # compiled wasm (the code cache) are written after the page is up
            session.send('Page.navigate', {'url': 'about:blank'})
            time.sleep(1.5)
        session.send('Page.removeScriptToEvaluateOnNewDocument', {'identifier': probe['identifier']})
        session.close()
        return visits
    finally:
        if browser:  # each example has its own browser: stop it, so it can't slow the next one
            browser.try_send('Browser.close')
            browser.close()
            time.sleep(0.5)


def ms(v):
    return '-' if v is None else str(round(v))


def kb(file):
    if not file:
        return '-'
    # a revalidated file transfers only its headers
    return 'cached' if file['transferred'] < 1024 else f'{round(file["transferred"] / 1024)} KB'


def main():
    opts = parse_args()
    manifest = opts.site / 'examples.json'
    if not manifest.exists():
        sys.exit(f'webstart: no web build at {opts.site}')
    built = json.loads(manifest.read_text())
    examples = opts.examples or built
    missing = [e for e in examples if e not in built]
    if missing:
        sys.exit(f'webstart: not built: {", ".join(missing)}')

    browser_path = None if opts.display == 'remote' else find_browser(opts.browser)
    run = RunProcesses('webstart')
    run.install_handlers()
    results = {}
    try:
        site_port = urllib.parse.urlsplit(opts.url).port if opts.url else free_port()
        run.spawn([PYTHON, ROOT / 'tools' / 'serve.py', site_port, opts.site, '--cache', '--gzip',
                   *(['--tls', *opts.tls] if opts.tls else [])])
        base_url = opts.url or f'http://127.0.0.1:{site_port}'
        if opts.tls:
            wait_for_port(site_port)  # its certificate isn't for 127.0.0.1
        else:
            wait_for(f'{base_url}/examples.json', 'tools/serve.py')
        where = {'headless': 'headless (SwiftShader)', 'xvfb': 'GPU, virtual display (Xvfb)',
                 'screen': 'GPU, on the screen', 'remote': f'the browser at DevTools port {opts.devtools}'}
        print(f'webstart: {len(examples)} example(s), {opts.backend}{"" if opts.threads == "1" else " no threads"}, '
              f'{where[opts.display]}{f", {browser_path}" if browser_path else ""}')
        print('ms from navigation; js and wasm show the bytes transferred (gzip)\n', flush=True)
        for net in opts.nets:
            print(f'network: {"local (no throttling)" if net == "none" else "emulated 4G (9 Mbit/s, 150 ms)"}')
            print(f'{"example":<14} {"visit":<5} {"js":>6} {"(xfer)":>9} {"wasm":>6} {"(xfer)":>9} {"compiled":>8} '
                  f'{"init":>6} {"libwgrender":>6} {"user":>6} {"fs":>6} {"frame":>6} {"ready":>6}', flush=True)
            for example in examples:
                try:
                    visits = measure(run, browser_path, base_url, example, net, opts)
                except RuntimeError as e:
                    print(f'{example:<14} failed: {e}', flush=True)
                    continue
                results.setdefault(example, {})[net] = visits
                for name in VISITS:
                    t = visits.get(name) or {}
                    js, wasm = t.get('js') or {}, t.get('wasm') or {}
                    ready = 'timeout' if t.get('ready') is None else ms(t['ready'])
                    print(f'{example if name == "cold" else "":<14} {name:<5} '
                          f'{ms(js.get("end")):>6} {kb(t.get("js")):>9} {ms(wasm.get("end")):>6} {kb(t.get("wasm")):>9} '
                          f'{ms(t.get("compiled")):>8} {ms(t.get("init")):>6} {ms(t.get("subsystems")):>6} '
                          f'{ms(t.get("user")):>6} {ms(t.get("fs")):>6} {ms(t.get("frame")):>6} {ready:>6}', flush=True)
            print()
        if opts.json:
            Path(opts.json).write_text(json.dumps({'backend': opts.backend, 'threads': opts.threads == '1',
                                                   'results': results}, indent=2))
        return 0
    finally:
        run.stop()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'webstart: {e}')
