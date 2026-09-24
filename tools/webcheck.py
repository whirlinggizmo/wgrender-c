#!/usr/bin/env python3
"""Web smoke check for the libwgrender examples (tools/verify.py --web runs it).

    tools/webcheck.py [options] [example ...]     (default: all built examples)

Serves build/web-<backend> with tools/serve.py, loads each built example in a
Chromium-based browser (Brave, Chrome, Chromium, Edge) through the DevTools protocol,
and fails an example if it logs a console error or a libwgrender [ERROR]/[FATAL] line,
throws, hits a sokol panic, never reports starting on the expected backend, or is
still loading assets when its time runs out. A screenshot of every example is saved
for a visual check. Standard library only (tools/weblib.py).

  --backend=webgl2|webgpu  backend to check (default webgl2); the site is
                      build/web-<backend> (the CMake preset of that name)
  --threads=0         the -nothreads build
  --headed            show the browser window on the real screen. Otherwise WebGL2 runs
                      headless, and WebGPU (which gets no working GPU device headless)
                      runs on a private virtual X display (Xvfb, ANGLE on Vulkan), so it
                      never shows a window or wakes the monitors. Without Xvfb (on
                      Windows and macOS always), WebGPU runs on the real screen.
  --settle=MS         longest an example runs before it is checked (default 20000). An
                      example is checked once it has started, has no asset tasks pending
                      (libwgrender's queue) and no network requests in flight, and has run
                      --quiet ms since the last of those changed
  --quiet=MS          (default 1500)
  --jobs=N            examples checked at once (default 4). Each example has its own
                      browser context (own storage; its own window when headed)
  --out=DIR           screenshot directory (default build/web-<backend>/webcheck)
  --browser=PATH      browser executable (or WEBCHECK_BROWSER; default: the first of
                      Brave, Chrome, Chromium and Edge found on PATH or where they install)
  --verbose           print every console line and browser log entry an example
                      produced (WebGPU validation messages arrive as log entries, not
                      console calls, so this is how to see them)

The browser and server are always stopped, including when this is interrupted or
killed (RunProcesses in tools/weblib.py).

Never call canvas.getContext() from here: a canvas that already has a WebGL context
can't be used for WebGPU, which breaks the example under test.
"""
import argparse
import base64
import json
import re
import sys
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from weblib import (PYTHON, ROOT, RunProcesses, find_browser, find_xvfb, free_port, launch_browser,
                    open_session, wait_for)

BACKEND_LOG = {'webgl2': 'GLES3/WebGL2 backend', 'webgpu': 'WebGPU backend'}
ERROR_LINE = re.compile(r'\[(ERROR|FATAL)')


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--backend', default='webgl2', choices=sorted(BACKEND_LOG))
    ap.add_argument('--threads', default='1', choices=['0', '1'])
    ap.add_argument('--headed', action='store_true')
    ap.add_argument('--settle', type=int, default=20000)
    ap.add_argument('--quiet', type=int, default=1500)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--out')
    ap.add_argument('--browser')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('examples', nargs='*')
    opts = ap.parse_args()
    # where the browser shows its windows: headless, xvfb or screen
    opts.display = ('screen' if opts.headed else
                    ('xvfb' if find_xvfb() else 'screen') if opts.backend == 'webgpu' else 'headless')
    opts.site = ROOT / 'build' / f'web-{opts.backend}{"" if opts.threads == "1" else "-nothreads"}'
    opts.jobs = max(1, opts.jobs)
    opts.out = Path(opts.out) if opts.out else opts.site / 'webcheck'
    return opts


def first_line(text):
    return text.strip().split('\n')[0]


def check_example(browser, debug_base, base_url, example, opts):
    result = {'example': example, 'errors': [], 'started': False, 'backend_ok': False, 'console': [],
              'pending': -1, 'start_ms': None}
    # Each example gets its own browser context (separate storage, like a fresh
    # profile), so examples checked at the same time don't share the IndexedDB file
    # cache and can't slow or affect each other.
    context = browser.send('Target.createBrowserContext', {'disposeOnDetach': True})['browserContextId']
    target = browser.send('Target.createTarget', {'url': 'about:blank', 'browserContextId': context})['targetId']
    session = open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{target}')
    lock = threading.Lock()
    inflight = set()
    state = {'navigated': time.monotonic(), 'activity': time.monotonic()}

    def on_event(msg):
        method, params = msg['method'], msg.get('params', {})
        with lock:
            if method == 'Network.requestWillBeSent':
                # a worker's own script (threaded builds start pthread workers) has no
                # loader, and its completion is reported to the worker, not this page
                if not params.get('loaderId'):
                    return
                inflight.add(params['requestId'])
                state['activity'] = time.monotonic()
            elif method in ('Network.loadingFinished', 'Network.loadingFailed'):
                inflight.discard(params['requestId'])
                state['activity'] = time.monotonic()
            elif method == 'Runtime.consoleAPICalled':
                text = ' '.join(str(a['value']) if 'value' in a else a.get('description', '')
                                for a in params['args'])
                result['console'].append(first_line(text))
                if 'libwgrender:' in text and 'backend' in text:
                    result['started'] = True
                    if result['start_ms'] is None:
                        result['start_ms'] = (time.monotonic() - state['navigated']) * 1000
                    state['activity'] = time.monotonic()
                    result['backend_ok'] = result['backend_ok'] or BACKEND_LOG[opts.backend] in text
                # libwgrender logs go to the console as plain messages: fail on error-level
                # ones like tools/smoke.py does ([ERROR], [FATAL])
                if params.get('type') == 'error' or '[panic]' in text or ERROR_LINE.search(text):
                    result['errors'].append(first_line(text))
            elif method == 'Runtime.exceptionThrown':
                d = params['exceptionDetails']
                result['errors'].append(first_line((d.get('exception') or {}).get('description')
                                                   or d.get('text') or 'exception'))
            elif method == 'Log.entryAdded':
                # the browser's own messages: WebGPU validation errors from Dawn, GL driver
                # warnings, network failures. Error-level ones fail the check, except
                # network ones: a missing favicon is a 404 too, and a missing asset
                # already fails through libwgrender's own loading errors
                e = params['entry']
                line = f'[{e["source"]}/{e["level"]}] {first_line(e.get("text") or "")}'
                result['console'].append(line)
                if e['level'] == 'error' and e['source'] != 'network':
                    result['errors'].append(line)

    session.on_event(on_event)
    try:
        for domain in ('Runtime', 'Log', 'Page', 'Network'):
            session.send(f'{domain}.enable')
        deadline = time.monotonic() + opts.settle / 1000
        state['navigated'] = time.monotonic()
        session.send('Page.navigate', {'url': f'{base_url}/?ex={urllib.parse.quote(example)}'})
        # Wait until the example has started, its asset downloads are done, and it has
        # run for a while since (errors from loading show up in that window), or until
        # the deadline, whichever comes first.
        pending = -1
        while time.monotonic() < deadline:
            if not result['started']:
                # don't call into the page before libwgrender reports it's running: calling
                # an exported function before the wasm runtime is initialized aborts the page
                time.sleep(0.1)
                continue
            value = session.send('Runtime.evaluate', {
                'expression': "typeof Module !== 'undefined' && Module._wgri_asset_pending_count"
                              " ? Module._wgri_asset_pending_count() : -1",
                'returnByValue': True})['result'].get('value')
            with lock:
                if value != pending:
                    pending = value
                    state['activity'] = time.monotonic()
                quiet = time.monotonic() - state['activity'] >= opts.quiet / 1000
                done = pending == 0 and not inflight and quiet
            if done:
                break
            time.sleep(0.1)
        result['pending'] = pending
        if pending and pending > 0:  # out of time: have libwgrender say which files, and where they are stuck
            session.try_send('Runtime.evaluate', {
                'expression': "typeof Module !== 'undefined' && Module._wgri_asset_pending_log"
                              " && Module._wgri_asset_pending_log()"})
            time.sleep(0.3)  # the warnings arrive as console events
        shot = session.send('Page.captureScreenshot', {'format': 'png'})
        result['screenshot'] = opts.out / f'{example}.png'
        result['screenshot'].write_bytes(base64.b64decode(shot['data']))
    finally:
        session.close()
        browser.try_send('Target.closeTarget', {'targetId': target})
        browser.try_send('Target.disposeBrowserContext', {'browserContextId': context})
    return result


def problems_of(r, opts):
    problems = list(r['errors'])
    if not r['started']:
        problems.append('never logged its backend (did not start?); last console output:')
        problems += [f'  | {line}' for line in r['console'][-6:]]
    elif r['pending'] is None or r['pending'] < 0:
        problems.append(f'never reported its asset queue in {opts.settle} ms (the runtime was still starting)')
    elif r['pending'] != 0:
        problems.append(f'still loading after {opts.settle} ms ({r["pending"]} asset task(s) pending):')
        stuck = [line for line in r['console'] if 'wgr_asset: pending:' in line]
        problems += [f'  | {line}' for line in (stuck or r['console'][-6:])]
    elif not r['backend_ok']:
        problems.append(f"started on a different backend than '{opts.backend}' (stale build?)")
    return problems


def main():
    opts = parse_args()
    manifest = opts.site / 'examples.json'
    if not manifest.exists():
        sys.exit(f'webcheck: no web build at {opts.site} (build it first: cmake --preset {opts.site.name} '
                 f'&& cmake --build --preset {opts.site.name})')
    built = json.loads(manifest.read_text())
    examples = opts.examples or built
    missing = [e for e in examples if e not in built]
    if missing:
        sys.exit(f'webcheck: not built: {", ".join(missing)}')

    browser_path = find_browser(opts.browser)
    opts.out.mkdir(parents=True, exist_ok=True)
    run = RunProcesses('webcheck')
    run.install_handlers()
    try:
        site_port = free_port()
        run.spawn([PYTHON, ROOT / 'tools' / 'serve.py', site_port, opts.site])
        base_url = f'http://127.0.0.1:{site_port}'
        wait_for(f'{base_url}/examples.json', 'tools/serve.py')
        if opts.display == 'screen' and opts.backend == 'webgpu' and not opts.headed:
            print('webcheck: no Xvfb; WebGPU runs in a window on the real screen')
        debug_base, browser = launch_browser(run, browser_path, opts.display)
        where = 'virtual display (Xvfb)' if opts.display == 'xvfb' else opts.display
        print(f'webcheck: {len(examples)} example(s), backend {opts.backend}, {where}, '
              f'{opts.jobs} at a time, {browser_path}', flush=True)

        # check `jobs` examples at a time; report in order as results complete
        results = [None] * len(examples)
        lock = threading.Lock()
        progress = {'next': 0, 'reported': 0, 'failed': 0}

        def report():
            with lock:
                while progress['reported'] < len(examples) and results[progress['reported']]:
                    r = results[progress['reported']]
                    progress['reported'] += 1
                    problems = problems_of(r, opts)
                    if problems:
                        progress['failed'] += 1
                    start = f' (started after {r["start_ms"] / 1000:.1f} s)' if r.get('start_ms') else ''
                    print(f'  {"FAIL" if problems else "ok  "}  {r["example"]}{start if problems else ""}')
                    for p in problems:
                        print(f'          {p}')
                    if opts.verbose:
                        for line in r['console']:
                            print(f'          | {line}')
                    sys.stdout.flush()

        def worker():
            while True:
                with lock:
                    index = progress['next']
                    if index >= len(examples):
                        return
                    progress['next'] += 1
                try:
                    results[index] = check_example(browser, debug_base, base_url, examples[index], opts)
                except Exception as e:
                    results[index] = {'example': examples[index], 'errors': [f'check failed: {e}'],
                                      'started': True, 'backend_ok': True, 'pending': 0, 'console': []}
                report()

        workers = [threading.Thread(target=worker) for _ in range(min(opts.jobs, len(examples)))]
        for w in workers:
            w.start()
        for w in workers:
            w.join()
        browser.close()
        print(f'screenshots: {opts.out}')
        failed = progress['failed']
        print(f'FAIL: {failed} of {len(examples)} example(s)' if failed else f'PASS: {len(examples)} example(s)')
        return 1 if failed else 0
    finally:
        run.stop()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'webcheck: {e}')
