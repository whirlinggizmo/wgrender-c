#!/usr/bin/env python3
"""Run a Windows program under Wine: the windows-x64-mingw-* presets' tests and smoke runs use it
(it is their CMAKE_CROSSCOMPILING_EMULATOR).

    tools/run_windows_program.py program.exe [args...]

Which Wine: $WINE if set, else wine64 or wine on PATH, else the newest Proton in a
Steam library (its files/bin/wine; Proton is Valve's Wine, installed from Steam's
Library > Tools). The Wine prefix (its fake C: drive and registry, shared by every build)
is wine/ in the per-user cache (tools/hostcache.py: ~/.cache/wgrender/wine on Linux)
unless WINEPREFIX says otherwise; it's made on first use, which takes a few seconds. Wine's
own debug output is off unless WINEDEBUG is set. Exits with the program's exit code,
or 127 when there's no Wine.
"""
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from hostcache import cache_dir  # noqa: E402
import wine as wines  # noqa: E402  (which Wine)

ROOT = Path(__file__).resolve().parents[1]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    wine = wines.find()
    if not wine:
        print('run_windows_program.py: no Wine found (install wine64, or Proton from Steam\'s Library > Tools, '
              'or set WINE)', file=sys.stderr)
        sys.exit(127)
    env = dict(os.environ)
    if 'WINEPREFIX' not in env:
        env['WINEPREFIX'] = str(cache_dir('wine'))
    env.setdefault('WINEDEBUG', '-all')
    Path(env['WINEPREFIX']).mkdir(parents=True, exist_ok=True)
    sys.exit(subprocess.run([wine, *sys.argv[1:]], env=env).returncode)


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
    import cli  # noqa: E402  (tools/cli.py: --help, and no argument it doesn't take)
    cli.parse(__doc__, (), positional=None, argv=sys.argv[1:2])
    main()
