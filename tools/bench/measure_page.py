#!/usr/bin/env python3
"""Measure one page's frame cost, JS allocation, garbage collection, or wgr call count
(tools/bench/pages.py, which has the details).

    python3 tools/bench/measure_page.py frame --site out/wasm32/release/site --url /?ex=simple --label c
    python3 tools/bench/measure_page.py gc    --site ... --label haxe-js --display xvfb
    python3 tools/bench/measure_page.py calls --site ... --label haxe-js
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import pages  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', choices=['frame', 'gc', 'calls'])
    ap.add_argument('--site', default='out/wasm32/release/site')
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
        result = pages.frame(a.site, a.label or 'bench', sample=a.sample or 8000, **common)
    elif a.what == 'gc':
        result = pages.gc(a.site, a.label or 'gcbench', sample=a.sample or 10000, load=a.load, uncapped=a.uncapped, **common)
    else:
        result = pages.calls(a.site, a.label or 'callcount', guest=a.guest, sample=a.sample or 8000, **common)
    print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as e:
        sys.exit(f'measure_page: {e}')
