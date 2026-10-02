"""sokol-shdc, the shader compiler: the build deps/sokol/VERSION pins (sokol-tools-bin),
for this OS, downloaded into the per-user cache (tools/hostcache.py) the first time and
checked against its SHA-256 below, every time it is used. It comes from floooh's
repository, or from the mirror (robknopf/sokol-tools-bin, a fork that keeps the pin
tagged) when floooh's can't be reached. To move to a newer build, change the pin in
deps/sokol/VERSION, HASHES_PIN and the hashes together, and tag the new pin in the
mirror (pin-<first 12 characters>).

    import shdc
    subprocess.run([shdc.path(), '-i', 'x.glsl', ...])

Standard library only.
"""
import hashlib
import os
import platform
import stat
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from hostcache import cache_dir  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]

SOURCES = ('floooh/sokol-tools-bin', 'robknopf/sokol-tools-bin')  # tried in order
HASHES_PIN = '11d0cf678105d614d675e6d9bd2aaf3eeff12f8c'  # the sokol-tools-bin commit these are for
SHA256 = {  # the folder in sokol-tools-bin's bin/: the program's SHA-256
    'linux': 'ed35e89ef381d521a499096ed4ada85e4d135d8011e151cca6b7d893c43b21df',
    'linux_arm64': '446b4bcea0c81d3ae529bc0d93533ea661b017f5b9ec2b2293a4c85f5fdcb639',
    'osx': '8b4a6ac1172ec0d90dd41d611067d5e87a51e78dc28acb216cfc341d880b1d78',
    'osx_arm64': '92db37975ad7ff3c3c9bc27cba1503287377cb287ebabf60d1c6b597abfa3244',
    'win32': 'bd616287f9ea689d53c6d260e443ee733e61ae1b73a9b37adc482ead0364d561',
}


def sha256_of(data):
    return hashlib.sha256(data).hexdigest()


def path():
    """The pinned sokol-shdc, fetched the first time and checked every time."""
    pin = next(l.split()[1] for l in (ROOT / 'deps/sokol/VERSION').read_text().splitlines()
               if l.startswith('sokol-tools-bin '))
    if pin != HASHES_PIN:
        sys.exit(f'shdc: deps/sokol/VERSION pins sokol-tools-bin {pin}, but tools/shdc.py has the '
                 f'hashes of {HASHES_PIN}: update HASHES_PIN and SHA256 together with the pin')
    system, machine = platform.system(), platform.machine().lower()
    folder = {'Linux': 'linux_arm64' if machine in ('aarch64', 'arm64') else 'linux',
              'Darwin': 'osx_arm64' if machine == 'arm64' else 'osx',
              'Windows': 'win32'}[system]
    exe = 'sokol-shdc.exe' if system == 'Windows' else 'sokol-shdc'
    path = cache_dir('tools') / f'sokol-shdc-{pin[:12]}' / exe
    if path.exists() and sha256_of(path.read_bytes()) == SHA256[folder]:
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    data, failures = None, []
    for source in SOURCES:
        url = f'https://raw.githubusercontent.com/{source}/{pin}/bin/{folder}/{exe}'
        print(f'shdc: downloading {url}', flush=True)
        try:
            with urllib.request.urlopen(url, timeout=120) as r:
                fetched = r.read()
        except OSError as e:  # unreachable, or gone: the next source
            failures.append(f'{url}: {e}')
            continue
        digest = sha256_of(fetched)
        if digest != SHA256[folder]:  # never used, wherever it came from
            failures.append(f'{url}: SHA-256 {digest}, not the pinned {SHA256[folder]}')
            continue
        data = fetched
        break
    if data is None:
        sys.exit('shdc: no source gave the pinned sokol-shdc:\n  ' + '\n  '.join(failures))
    partial = path.with_name(path.name + '.part')  # in place only once it is whole
    partial.write_bytes(data)
    if os.name != 'nt':
        partial.chmod(partial.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    partial.replace(path)
    return path
