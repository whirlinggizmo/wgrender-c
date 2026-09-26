#!/usr/bin/env python3
"""The web asset cache, end to end (docs/PLAN-asset-cache.md): the bug of 2026-09-25,
reproduced and shown fixed.

    tools/cachecheck.py [--manifest] [--backend=webgl2|webgpu] [--threads=0] [--browser=PATH]
                        [--verbose]

Serves a web build with tools/serve.py, with /assets/ mounted from a scratch copy of
the one file the tilemap example loads (textures/tiles.png), and visits tilemap
again and again in one browser context, so its IndexedDB cache carries over from
visit to visit as a returning visitor's does. Between visits the file changes, and
each visit is judged by every request it made under /assets/ (and its HTTP status),
what libwgrender logged, and whether the screen shows the replacement sheet, which
is solid magenta. Without a manifest (the examples ask for manifest.json, and get a
404), each cached copy is asked about:

  first      downloaded (200), the real sheet on screen
  unchanged  asked about and kept (304)
  offline    /assets/ blocked: the cached copy is used, no error
  changed    the sheet replaced on disk: downloaded again (200), magenta on screen
  again      the new copy kept (304), still magenta
  gone       the file deleted: 404, the cached copy forgotten, the load fails
  gone, offline  /assets/ blocked: nothing cached any more, so it fails again

--manifest writes the manifests (tools/gen_manifest.py) after each change, and only
the root manifest is asked about:

  first      the root, textures/manifest.json and the sheet downloaded
  unchanged  the root asked about (304), nothing else requested
  offline    the root blocked: the cached one is used, and the cached sheet
  changed    the root, the directory's manifest and the sheet downloaded; magenta
  again      the root asked about (304), still magenta, nothing else requested
  stale host the manifests list a green sheet, the host serves magenta: downloaded,
             not kept, the load fails
  caught up  the host serves the green sheet: only it is downloaded

Default serving (no-store), so every copy is stale and every visit asks: the
revalidation path, which is the one that fixes the bug. Standard library only.
"""
import argparse
import base64
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import builds  # noqa: E402
from weblib import (PYTHON, ROOT, RunProcesses, find_browser, find_xvfb, free_port, launch_browser,  # noqa: E402
                    open_session, wait_for)

EXAMPLE = 'tilemap'
TILES = 'textures/tiles.png'
MAGENTA_MIN = 2000  # pixels: the tile map covers much of the screen when the sheet draws


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--manifest', action='store_true', help='with manifests (tools/gen_manifest.py)')
    ap.add_argument('--backend', default='webgl2', choices=('webgl2', 'webgpu'))
    ap.add_argument('--threads', default='1', choices=('0', '1'))
    ap.add_argument('--browser')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--settle', type=int, default=20000, help='longest a visit runs, ms')
    ap.add_argument('--quiet', type=int, default=1500, help='quiet time that ends a visit, ms')
    opts = ap.parse_args()
    opts.site = builds.out(builds.web(opts.backend, opts.threads == '1'))
    opts.display = 'headless' if opts.backend == 'webgl2' else ('xvfb' if find_xvfb() else 'screen')
    return opts


def solid_png(width, height, rgba):
    """A PNG of one colour, `width` x `height`."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    row = b'\0' + bytes(rgba) * width
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(row * height)) + chunk(b'IEND', b''))


def png_size(data):
    return struct.unpack('>II', data[16:24])


def magenta_pixels(session, png_base64):
    """Pixels of a screenshot that are (near) magenta, decoded in the page on a canvas of
    its own (never the example's)."""
    expression = f"""(async () => {{
        const image = await createImageBitmap(await (await fetch("data:image/png;base64,{png_base64}")).blob());
        const canvas = new OffscreenCanvas(image.width, image.height);
        const context = canvas.getContext("2d");
        context.drawImage(image, 0, 0);
        const p = context.getImageData(0, 0, image.width, image.height).data;
        let n = 0;
        for (let i = 0; i < p.length; i += 4)
            if (p[i] > 200 && p[i + 1] < 60 && p[i + 2] > 200) n++;
        return n;
    }})()"""
    return session.send('Runtime.evaluate', {'expression': expression, 'awaitPromise': True,
                                             'returnByValue': True}, 30)['result']['value']


class Visitor:
    """One tab in one browser context, visited again and again (the context's storage,
    IndexedDB included, stays between visits)."""

    def __init__(self, browser, debug_base, base_url, opts):
        self.browser, self.base_url, self.opts = browser, base_url, opts
        self.context = browser.send('Target.createBrowserContext')['browserContextId']
        self.target = browser.send('Target.createTarget', {'url': 'about:blank',
                                                          'browserContextId': self.context})['targetId']
        self.session = open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{self.target}')
        self.lock = threading.Lock()
        self.visit_state = None
        self.session.on_event(self.on_event)
        for domain in ('Runtime', 'Log', 'Page', 'Network'):
            self.session.send(f'{domain}.enable')

    def on_event(self, msg):
        method, params = msg['method'], msg.get('params', {})
        with self.lock:
            v = self.visit_state
            if v is None:
                return
            now = time.monotonic()
            if method == 'Network.requestWillBeSent':
                if params.get('loaderId'):
                    v['inflight'].add(params['requestId'])
                    v['urls'][params['requestId']] = params['request']['url']
                    v['activity'] = now
            elif method == 'Network.responseReceived':
                path = urllib.parse.urlsplit(params['response']['url']).path
                v['status'].setdefault(path, []).append(params['response']['status'])
                v['answered'].add(params['requestId'])
            elif method in ('Network.loadingFinished', 'Network.loadingFailed'):
                v['inflight'].discard(params['requestId'])
                v['activity'] = now
                # a 304's body is never read, and DevTools calls that a failed load: only
                # a request that got no answer at all failed
                if method == 'Network.loadingFailed' and params['requestId'] not in v['answered']:
                    url = v['urls'].get(params['requestId'], '')
                    v['status'].setdefault(urllib.parse.urlsplit(url).path, []).append('failed')
            elif method == 'Runtime.consoleAPICalled':
                text = ' '.join(str(a['value']) if 'value' in a else a.get('description', '')
                                for a in params['args']).strip().split('\n')[0]
                v['console'].append(text)
                if 'libwgrender:' in text and 'backend' in text:
                    v['started'] = True
                    v['activity'] = now
            elif method == 'Runtime.exceptionThrown':
                v['console'].append('exception: ' + params['exceptionDetails'].get('text', ''))

    def visit(self, blocked=()):
        self.session.send('Network.setBlockedURLs', {'urls': list(blocked)})
        with self.lock:
            self.visit_state = {'inflight': set(), 'urls': {}, 'status': {}, 'answered': set(), 'console': [],
                                'started': False, 'activity': time.monotonic()}
        self.session.send('Page.navigate', {'url': f'{self.base_url}/?ex={EXAMPLE}'})
        deadline = time.monotonic() + self.opts.settle / 1000
        pending = -1
        while time.monotonic() < deadline:
            with self.lock:
                started = self.visit_state['started']
            if not started:
                time.sleep(0.1)
                continue
            value = self.session.send('Runtime.evaluate', {
                'expression': "typeof Module !== 'undefined' && Module._wgri_asset_pending_count"
                              " ? Module._wgri_asset_pending_count() : -1", 'returnByValue': True})['result'].get('value')
            with self.lock:
                v = self.visit_state
                if value != pending:
                    pending = value
                    v['activity'] = time.monotonic()
                if pending == 0 and not v['inflight'] and time.monotonic() - v['activity'] >= self.opts.quiet / 1000:
                    break
            time.sleep(0.1)
        shot = self.session.send('Page.captureScreenshot', {'format': 'png'})['data']
        with self.lock:
            v, self.visit_state = self.visit_state, None
        v['pending'] = pending
        v['magenta'] = magenta_pixels(self.session, shot)
        v['shot'] = shot
        return v

    def close(self):
        self.session.close()
        self.browser.try_send('Target.closeTarget', {'targetId': self.target})
        self.browser.try_send('Target.disposeBrowserContext', {'browserContextId': self.context})


def judge(v, requests, magenta, failed=False, log=None):
    """What's wrong with a visit, as lines. `requests`: path under /assets/ -> the
    status of its one request ('failed': no answer); any other asset request is wrong."""
    problems = []
    got = {path[len('/assets/'):]: statuses for path, statuses in v['status'].items() if path.startswith('/assets/')}
    for path in sorted(set(got) | set(requests)):
        want = [requests[path]] if path in requests else []
        if got.get(path, []) != want:
            problems.append(f'{path}: expected {want or "no request"}, got {got.get(path) or "no request"}')
    if v['pending'] != 0:
        problems.append(f'still loading ({v["pending"]} asset task(s) pending)')
    if magenta and v['magenta'] < MAGENTA_MIN:
        problems.append(f'the replacement sheet is not on screen ({v["magenta"]} magenta pixels)')
    if not magenta and v['magenta'] >= MAGENTA_MIN:
        problems.append(f'the replacement sheet is on screen ({v["magenta"]} magenta pixels)')
    errors = [line for line in v['console'] if '[ERROR' in line or '[FATAL' in line or line.startswith('exception')]
    if failed and not any(TILES in line for line in errors):
        problems.append(f'the load of {TILES} did not fail')
    if not failed and errors:
        problems += [f'error: {line}' for line in errors]
    if log and not any(log in line for line in v['console']):
        problems.append(f'libwgrender did not log "{log}"')
    return problems


def main():
    opts = parse_args()
    if not (opts.site / 'examples.json').exists():
        preset = builds.preset_of(opts.site)
        sys.exit(f'cachecheck: no web build at {opts.site} (cmake --preset {preset} && cmake --build --preset {preset})')
    work = builds.work(builds.preset_of(opts.site)) / 'cachecheck'
    assets = work / 'assets'
    shutil.rmtree(work, ignore_errors=True)
    (assets / TILES).parent.mkdir(parents=True)
    tiles = assets / TILES
    original = (ROOT / 'examples' / 'assets' / TILES).read_bytes()
    tiles.write_bytes(original)

    run = RunProcesses('cachecheck')
    run.install_handlers()
    failed = 0
    try:
        port = free_port()
        run.spawn([PYTHON, ROOT / 'tools' / 'serve.py', port, opts.site, '--assets', assets])
        base_url = f'http://127.0.0.1:{port}'
        wait_for(f'{base_url}/examples.json', 'tools/serve.py')
        debug_base, browser = launch_browser(run, find_browser(opts.browser), opts.display)
        visitor = Visitor(browser, debug_base, base_url, opts)
        blocked = [f'{base_url}/assets/*']
        clock = {'later': time.time()}
        magenta = solid_png(*png_size(original), (255, 0, 255, 255))
        green = solid_png(*png_size(original), (0, 160, 0, 255))

        def touch(path):
            """A modification time past every earlier one: Last-Modified counts whole
            seconds, and a change within one would answer 304."""
            clock['later'] = max(clock['later'], time.time()) + 2
            os.utime(path, (clock['later'], clock['later']))

        def serve(data):
            tiles.write_bytes(data)
            touch(tiles)

        def deploy():
            subprocess.run([PYTHON, ROOT / 'tools' / 'gen_manifest.py', assets, '--quiet'], check=True)
            for manifest in assets.rglob('manifest.json'):
                touch(manifest)

        M, D, T = 'manifest.json', 'textures/manifest.json', TILES
        if opts.manifest:
            def stale_host():
                serve(green)
                deploy()
                serve(magenta)  # the manifests list green

            deploy()
            steps = [
                ('first', None, lambda v: judge(v, {M: 200, D: 200, T: 200}, False)),
                ('unchanged', None, lambda v: judge(v, {M: 304}, False)),
                ('offline', 'block', lambda v: judge(v, {M: 'failed'}, False)),
                ('changed', lambda: (serve(magenta), deploy()), lambda v: judge(v, {M: 200, D: 200, T: 200}, True)),
                ('again', None, lambda v: judge(v, {M: 304}, True)),
                ('stale host', stale_host, lambda v: judge(v, {M: 200, D: 200, T: 200}, False, failed=True,
                                                          log="isn't what the manifest lists")),
                ('caught up', lambda: serve(green), lambda v: judge(v, {M: 304, T: 200}, False)),
            ]
        else:
            steps = [
                ('first', None, lambda v: judge(v, {M: 404, T: 200}, False)),
                ('unchanged', None, lambda v: judge(v, {M: 404, T: 304}, False)),
                ('offline', 'block', lambda v: judge(v, {M: 'failed', T: 'failed'}, False)),
                ('changed', lambda: serve(magenta), lambda v: judge(v, {M: 404, T: 200}, True)),
                ('again', None, lambda v: judge(v, {M: 404, T: 304}, True)),
                ('gone', tiles.unlink, lambda v: judge(v, {M: 404, T: 404}, False, failed=True,
                                                      log='gone from the host')),
                ('gone, offline', 'block', lambda v: judge(v, {M: 'failed', T: 'failed'}, False, failed=True)),
            ]
        print(f'cachecheck: {EXAMPLE} on {opts.site}, {opts.display}{", with manifests" if opts.manifest else ""}',
              flush=True)
        for name, before, check in steps:
            if callable(before):
                before()
            v = visitor.visit(blocked if before == 'block' else ())
            problems = check(v)
            failed += 1 if problems else 0
            print(f'  {"FAIL" if problems else "ok  "}  {name}')
            for p in problems:
                print(f'          {p}')
            if opts.verbose or problems:
                for line in v['console']:
                    print(f'          | {line}')
            (work / f'{name.replace(", ", "-")}.png').write_bytes(base64.b64decode(v['shot']))
        visitor.close()
        browser.close()
        print(f'screenshots: {work}')
        print(f'FAIL: {failed} of {len(steps)} visit(s)' if failed else f'PASS: {len(steps)} visits')
        return 1 if failed else 0
    finally:
        run.stop()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'cachecheck: {e}')
