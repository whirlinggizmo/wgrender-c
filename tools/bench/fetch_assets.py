#!/usr/bin/env python3
"""Download the loading benchmark's models (glTF Sample Assets, ~100 MB, not in the
repository) into examples/assets/bench/. Sponza: CC BY 4.0 (Crytek, Frank Meinl);
FlightHelmet: CC0. See https://github.com/KhronosGroup/glTF-Sample-Assets.

    tools/bench/fetch_assets.py
"""
import json
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'examples' / 'assets' / 'bench'
API = 'https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/contents/Models'


def get(url, timeout):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=timeout) as r:
                return r.read()
        except OSError:
            if attempt == 3:
                raise
            time.sleep(1 + attempt)


def main():
    for model in ('Sponza', 'FlightHelmet'):
        folder = DEST / model
        if (folder / '.complete').exists():
            print(f'bench: {model} already downloaded')
            continue
        folder.mkdir(parents=True, exist_ok=True)
        print(f'bench: downloading {model}', flush=True)
        entries = json.loads(get(f'{API}/{model}/glTF', 60))
        files = [(e['name'], e['download_url']) for e in entries if e['type'] == 'file']
        with ThreadPoolExecutor(max_workers=8) as pool:
            for name, data in pool.map(lambda f: (f[0], get(f[1], 300)), files):
                (folder / name).write_bytes(data)
        (folder / '.complete').touch()


if __name__ == '__main__':
    main()
