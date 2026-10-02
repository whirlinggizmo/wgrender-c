"""sokol-shdc, the shader compiler: the build deps/sokol/VERSION pins (sokol-tools-bin),
for this OS, downloaded into the per-user cache (tools/hostcache.py) the first time.

    import shdc
    subprocess.run([shdc.path(), '-i', 'x.glsl', ...])

Standard library only.
"""
import platform
import stat
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from hostcache import cache_dir  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def path():
    """The pinned sokol-shdc, fetched the first time."""
    pin = next(l.split()[1] for l in (ROOT / 'deps/sokol/VERSION').read_text().splitlines()
               if l.startswith('sokol-tools-bin '))
    system, machine = platform.system(), platform.machine().lower()
    folder = {'Linux': 'linux_arm64' if machine in ('aarch64', 'arm64') else 'linux',
              'Darwin': 'osx_arm64' if machine == 'arm64' else 'osx',
              'Windows': 'win32'}[system]
    exe = 'sokol-shdc.exe' if system == 'Windows' else 'sokol-shdc'
    path = cache_dir('tools') / f'sokol-shdc-{pin[:12]}' / exe
    if not path.exists():
        url = f'https://raw.githubusercontent.com/floooh/sokol-tools-bin/{pin}/bin/{folder}/{exe}'
        print(f'shdc: downloading {url}', flush=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=120) as r:
            path.write_bytes(r.read())
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


