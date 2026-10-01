#!/usr/bin/env python3
"""Startup timing for any wgrender web build, for sites wgrender's own
tools/webstart.py can't point at (it takes a web preset's site, with its
examples.json). The same idea, fewer options.

    tools/webstart.py --site=DIR [--site=DIR ...] [--net=local|4g|both] [--runs=N]

Each site is opened in a fresh profile, served the way a host should (tools/serve.py
--cache --gzip), and timed from navigation to libwgrender's own performance marks:
wgr:init, wgr:subsystems, wgr:user-init, wgr:fs-ready, wgr:first-frame. Transferred
bytes come from the resource timings. cold = a fresh profile, nothing cached; warm =
the second visit in the same profile. Medians of --runs (default 3).
"""
import argparse
import json
import statistics
import sys
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from wgrweb import serve_command, weblib  # noqa: E402

NETS = {'local': None, '4g': {'offline': False, 'latency': 150, 'downloadThroughput': 9e6 / 8,
                              'uploadThroughput': 1.5e6 / 8}}
MARKS = ['wgr:init', 'wgr:subsystems', 'wgr:user-init', 'wgr:fs-ready', 'wgr:first-frame']
PROBE = """JSON.stringify({
    marks: performance.getEntriesByType("mark").map((m) => [m.name, m.startTime]),
    bytes: performance.getEntriesByType("resource").reduce((n, r) => n + (r.transferSize || 0), 0),
})"""


def visit(page, url, net):
    page.send('Network.enable')
    page.send('Network.emulateNetworkConditions',
              net or {'offline': False, 'latency': 0, 'downloadThroughput': -1, 'uploadThroughput': -1})
    page.send('Page.navigate', {'url': url})
    deadline = time.monotonic() + 60
    while True:
        got = json.loads(page.send('Runtime.evaluate', {'expression': PROBE, 'returnByValue': True})['result']['value'])
        if any(name == 'wgr:first-frame' for name, _ in got['marks']) or time.monotonic() > deadline:
            return got
        time.sleep(0.1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--site', action='append', required=True)
    ap.add_argument('--net', default='both', choices=['local', '4g', 'both'])
    ap.add_argument('--runs', type=int, default=3)
    args = ap.parse_args()
    nets = ['local', '4g'] if args.net == 'both' else [args.net]
    results = []
    for site in (Path(s).resolve() for s in args.site):
        label = site.parent.parent.name
        for net in nets:
            run = weblib.RunProcesses(f'webstart-{label}')
            run.install_handlers()
            try:
                port = weblib.free_port()
                run.spawn(serve_command(port, site, '--cache', '--gzip'))
                entry = 'wgrender-host.js' if (site / 'wgrender-host.js').exists() else 'index.html'
                weblib.wait_for(f'http://127.0.0.1:{port}/{entry}', 'serve.py')
                for i in range(args.runs):
                    debug_base, browser = weblib.launch_browser(run, weblib.find_browser(), 'headless',
                                                                profile=f'{run.profile}-{net}-{i}')
                    target = browser.send('Target.createTarget', {'url': 'about:blank'})['targetId']
                    page = weblib.open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{target}')
                    page.send('Runtime.enable')
                    page.send('Page.enable')
                    url = f'http://127.0.0.1:{port}/'
                    cold = visit(page, url, NETS[net])
                    warm = visit(page, url, NETS[net])
                    results.append({'site': label, 'net': net, 'cold': cold, 'warm': warm})
                    browser.try_send('Browser.close')
                    browser.close()
                    time.sleep(0.3)
            finally:
                run.stop()

    def at(r, name):
        return next((t for n, t in r['marks'] if n == name), None)

    def median(xs):
        xs = [x for x in xs if x is not None]
        return statistics.median_low(xs) if xs else None

    for kind in ('cold', 'warm'):
        print(f'\n=== {kind} (ms to each mark, median of {args.runs}) ===')
        print(f'{"site":<12} {"net":<6} ' + ' '.join(f'{m.replace("wgr:", ""):>6}' for m in MARKS) + '   transferred')
        for label in dict.fromkeys(r['site'] for r in results):
            for net in nets:
                rows = [r[kind] for r in results if r['site'] == label and r['net'] == net]
                if not rows:
                    continue
                cells = '  '.join('    -' if (v := median([at(r, m) for r in rows])) is None else f'{v:5.0f}'
                                  for m in MARKS)
                transferred = median([r['bytes'] for r in rows])
                print(f'{label:<12} {net:<6} {cells}   {f"{transferred:,}" if transferred else "-"}')


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'webstart: {e}')
