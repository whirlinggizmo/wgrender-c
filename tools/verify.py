#!/usr/bin/env python3
"""Run before calling a change done: every build and test this machine can run.

    tools/verify.py [--only STEP[,STEP...]]

  desktop   the library and examples with a window, GPU and audio (built, not run)
  headless  unit tests, guardrails (tools/check.py) and a smoke run of every example
  tsan      the unit tests under ThreadSanitizer (Linux and macOS)
  windows           the Windows build cross-built with MinGW-w64, when it's installed
  windows-headless  its unit tests and smoke run under Wine, when there's a Wine too

Each step is a CMake preset (CMakePresets.json), configured, built and tested under
build/<preset>/. Stops at the first step that fails.
"""
import argparse
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def steps():
    yield 'desktop', False
    yield 'headless', True
    if platform.system() in ('Linux', 'Darwin'):
        yield 'tsan', True
        if shutil.which('x86_64-w64-mingw32-gcc'):
            yield 'windows', False
            sys.path.insert(0, str(ROOT / 'tools'))
            import wine
            if wine.find_wine():
                yield 'windows-headless', True
            else:
                print('verify: no Wine, so the Windows build is built but not run')


def run(*cmd):
    print('$ ' + ' '.join(cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT).returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='comma-separated steps to run')
    args = ap.parse_args()
    only = set(args.only.split(',')) if args.only else None

    for preset, test in steps():
        if only and preset not in only:
            continue
        print(f'== {preset}', flush=True)
        start = time.monotonic()
        ok = (run('cmake', '--preset', preset) and run('cmake', '--build', '--preset', preset)
              and (not test or run('ctest', '--preset', preset)))
        if not ok:
            sys.exit(f'verify: FAIL at {preset}')
        print(f'== {preset}: ok ({time.monotonic() - start:.0f}s)', flush=True)
    print('verify: PASS')


if __name__ == '__main__':
    main()
