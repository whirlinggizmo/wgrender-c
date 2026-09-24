#!/usr/bin/env python3
"""Run a Windows program under Wine: the windows-mingw* presets' tests and smoke runs use it
(it is their CMAKE_CROSSCOMPILING_EMULATOR).

    tools/wine.py program.exe [args...]

Which Wine: $WINE if set, else wine64 or wine on PATH, else the newest Proton in a
Steam library (its files/bin/wine; Proton is Valve's Wine, installed from Steam's
Library > Tools). The Wine prefix (its fake C: drive and registry, shared by every build)
is wine/ in the per-user cache (tools/hostcache.py: ~/.cache/wgrender/wine on Linux)
unless WINEPREFIX says otherwise; it's made on first use, which takes a few seconds. Wine's
own debug output is off unless WINEDEBUG is set. Exits with the program's exit code,
or 127 when there's no Wine.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from hostcache import cache_dir  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def version_key(path):
    """'Proton 11.0' after 'Proton 9.0'; Experimental and the like before numbers."""
    return [int(n) for n in re.findall(r'\d+', path.parent.parent.parent.name)]


def find_wine():
    if os.environ.get('WINE'):
        return os.environ['WINE']
    for name in ('wine64', 'wine'):
        if shutil.which(name):
            return shutil.which(name)
    home = Path.home()
    libraries = [home / '.local/share/Steam', home / '.steam/steam']
    vdf = home / '.local/share/Steam/steamapps/libraryfolders.vdf'
    if vdf.exists():
        libraries += [Path(p) for p in re.findall(r'"path"\s*"([^"]*)"', vdf.read_text(errors='replace'))]
    found = {w for lib in libraries for w in lib.glob('steamapps/common/Proton*/files/bin/wine')
             if os.access(w, os.X_OK)}
    return str(max(found, key=version_key)) if found else None


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    wine = find_wine()
    if not wine:
        print('wine.py: no Wine found (install wine64, or Proton from Steam\'s Library > Tools, '
              'or set WINE)', file=sys.stderr)
        sys.exit(127)
    env = dict(os.environ)
    if 'WINEPREFIX' not in env:
        env['WINEPREFIX'] = str(cache_dir('wine'))
    env.setdefault('WINEDEBUG', '-all')
    Path(env['WINEPREFIX']).mkdir(parents=True, exist_ok=True)
    sys.exit(subprocess.run([wine, *sys.argv[1:]], env=env).returncode)


if __name__ == '__main__':
    main()
