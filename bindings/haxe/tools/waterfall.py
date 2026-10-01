#!/usr/bin/env python3
"""Per-resource timings for one cold visit on emulated 4G: which requests happen when,
and what waits on what.

    tools/waterfall.py SITE          (e.g. examples/simple/out/web)

The site is served the way a host should (tools/serve.py --cache --gzip) and loaded in
a fresh headless browser; after 12 s it prints every request with its start, end and
bytes transferred, then libwgrender's performance marks.
"""
import json
import sys
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from wgrweb import serve_command, weblib  # noqa: E402

RESOURCES = r"""JSON.stringify(performance.getEntriesByType("resource")
    .filter(r => !/\/wgr\//.test(r.name))
    .map(r => [r.name.split("/").pop(), Math.round(r.startTime), Math.round(r.responseEnd), r.transferSize])
    .sort((a, b) => a[1] - b[1]))"""
MARKS = 'JSON.stringify(performance.getEntriesByType("mark").map(m => [m.name, Math.round(m.startTime)]))'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    site = Path(sys.argv[1]).resolve()
    label = site.parent.parent.name
    run = weblib.RunProcesses(f'waterfall-{label}')
    run.install_handlers()
    try:
        port = weblib.free_port()
        run.spawn(serve_command(port, site, '--cache', '--gzip'))
        weblib.wait_for(f'http://127.0.0.1:{port}/index.html', 'serve.py')
        debug_base, browser = weblib.launch_browser(run, weblib.find_browser(), 'headless')
        target = browser.send('Target.createTarget', {'url': 'about:blank'})['targetId']
        page = weblib.open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{target}')
        for domain in ('Runtime', 'Page', 'Network'):
            page.send(f'{domain}.enable')
        page.send('Network.emulateNetworkConditions',
                  {'offline': False, 'latency': 150, 'downloadThroughput': 9e6 / 8, 'uploadThroughput': 1.5e6 / 8})
        page.send('Page.navigate', {'url': f'http://127.0.0.1:{port}/'})
        time.sleep(12)
        resources = json.loads(page.send('Runtime.evaluate', {'expression': RESOURCES, 'returnByValue': True})
                               ['result']['value'])
        print(f'=== {label}: cold, 4G ===')
        print('  resource                start     end   bytes')
        for name, start, end, transferred in resources:
            print(f'  {name:<22} {start:>5}   {end:>5}   {transferred:>8,}')
        marks = json.loads(page.send('Runtime.evaluate', {'expression': MARKS, 'returnByValue': True})['result']['value'])
        print('  marks:', ' '.join(f'{n.replace("wgr:", "")}={t}' for n, t in marks))
    finally:
        run.stop()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'waterfall: {e}')
