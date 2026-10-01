#!/usr/bin/env python3
"""Load a web build in a headless browser, run it, and fail on anything the console
calls an error. examples/build.py drive runs it for every example:

    tools/drive.py --site=out/web/<variant> [--label=NAME] [--settle=MS] [--click] [--min-colours=N]
                   [--ready=FILE] [--shot=PATH]

It serves the site with tools/serve.py (wgrender's assets at /assets), waits --settle
ms (default 6000), moves the mouse over the middle of the canvas, a little low (over
whatever the example puts there, which exercises picking and the hover state a scene
keeps), with --click clicks there, and takes a screenshot (--shot, default the
example's build/web/<variant>/check.png: the example's work, not its site). --ready is
the file whose serving means the server is up (default wgrender-host.js, a guest's
host).

The screenshot is saved because three bugs in this port were visible there and
invisible to every assertion: a canvas with no CSS size, a model drawn off screen, and
a pivot taken as pixels instead of a fraction. And it is read, because "no console
error" passed a build that rendered nothing at all: a lifecycle bug wiped the frame
callback and this said ok over a black canvas. A screen of fewer than --min-colours
colours (default 2) is a failure whatever the console says; 0 turns that off.
"""
import argparse
import base64
import re
import sys
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from wgrweb import serve_command, weblib  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--site', required=True)
    ap.add_argument('--label', default='guest')
    ap.add_argument('--settle', type=int, default=6000)
    ap.add_argument('--click', action='store_true')
    ap.add_argument('--min-colours', type=int, default=2)
    ap.add_argument('--ready', default='wgrender-host.js')
    ap.add_argument('--shot')
    args = ap.parse_args()
    site = Path(args.site).resolve()
    # <example>/out/web/<variant> -> <example>/build/web/<variant>/check.png
    shot_path = Path(args.shot).resolve() if args.shot else site.parents[2] / 'build/web' / site.name / 'check.png'

    run = weblib.RunProcesses(args.label)
    run.install_handlers()
    errors, lines = [], []
    try:
        port = weblib.free_port()
        run.spawn(serve_command(port, site))
        weblib.wait_for(f'http://127.0.0.1:{port}/{args.ready}', 'serve.py')
        debug_base, browser = weblib.launch_browser(run, weblib.find_browser(), 'headless')
        target = browser.send('Target.createTarget', {'url': 'about:blank'})['targetId']
        page = weblib.open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{target}')

        def on_event(msg):
            params = msg.get('params', {})
            if msg['method'] == 'Runtime.consoleAPICalled':
                text = ' '.join(str(a['value']) if 'value' in a else a.get('description', '') for a in params['args'])
                lines.append(text)
                if params.get('type') == 'error' or re.search(r'\[(ERROR|FATAL)', text):
                    errors.append(text)
            elif msg['method'] == 'Runtime.exceptionThrown':
                d = params['exceptionDetails']
                errors.append('UNCAUGHT ' + ((d.get('exception') or {}).get('description') or d.get('text') or '')
                              .split('\n')[0])
            elif msg['method'] == 'Log.entryAdded' and params['entry']['level'] == 'error' \
                    and params['entry']['source'] != 'network':
                # the browser's own: a module script refused for its MIME type, a WebGL error
                errors.append(f'[{params["entry"]["source"]}] {params["entry"].get("text", "")}')

        page.on_event(on_event)
        for domain in ('Runtime', 'Log', 'Page'):
            page.send(f'{domain}.enable')
        page.send('Page.navigate', {'url': f'http://127.0.0.1:{port}/'})
        time.sleep(args.settle / 1000)
        centre = page.send('Runtime.evaluate', {
            'expression': "(() => { const r = document.getElementById('canvas').getBoundingClientRect();"
                          " return [r.left + r.width / 2, r.top + r.height / 2 + 20]; })()",
            'returnByValue': True})['result'].get('value')
        if centre:
            x, y = centre
            page.send('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y})
            if args.click:
                for kind in ('mousePressed', 'mouseReleased'):
                    page.send('Input.dispatchMouseEvent', {'type': kind, 'x': x, 'y': y, 'button': 'left',
                                                           'clickCount': 1})
        else:
            errors.append('no #canvas on the page')
        time.sleep(1)
        data = page.send('Page.captureScreenshot', {'format': 'png'})['data']
        shot_path.parent.mkdir(parents=True, exist_ok=True)
        shot_path.write_bytes(base64.b64decode(data))
        if args.min_colours > 0:
            colours = weblib.distinct_colours(page, data)
            if colours < args.min_colours:
                errors.append(f'the screen is {"one flat colour" if colours == 1 else f"only {colours} colours"}'
                              f': nothing was drawn ({shot_path})')
        for line in lines:
            print(f'  {line}')
        print(f'screenshot: {shot_path}')
    finally:
        run.stop()
    if errors:
        print('FAILED:\n  ' + '\n  '.join(errors), file=sys.stderr)
        return 1
    print('ok')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'drive: {e}')
