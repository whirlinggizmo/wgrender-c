#!/usr/bin/env python3
"""Run before calling a change done: every build and test this machine can run.

    tools/verify.py [--web] [--windows HOST] [--only STEP[,STEP...]]

This machine's own presets (linux-x64-*, macos-arm64-*, or windows-x64-msvc-* on Windows):
  release         the library and examples with a window, GPU and audio (built, not run)
  debug-headless  unit tests, guardrails (tools/check_rules.py) and a smoke run of every example
  debug-tsan      the unit tests under ThreadSanitizer (Linux and macOS)
and on Linux and macOS:
  windows-x64-mingw-release         the Windows build cross-built with MinGW-w64, when
                                    it's installed
  windows-x64-mingw-debug-headless  its unit tests and smoke run under Wine, when there's
                                    a Wine too
and when Haxe is installed:
  haxe      the Haxe binding's suite (bindings/haxe/test/check.py: its generators
            current, headless hxcpp, JS, cppia, its C)
With --web, also (needs Emscripten, and a Chromium-based browser: Brave, Chrome,
Chromium or Edge):
  wasm32-release, wasm32-release-threads, wasm32-release-webgpu-threads
                    every example built for the web and loaded in the browser
                    (tools/check_web.py)
  haxe-web  the Haxe examples built for the web and driven in the browser, when Haxe
            is installed
With --windows HOST, also, on that Windows machine over ssh (tools/run_remote_windows.py:
the working tree as it is, nothing left there):
  windows-mingw  windows-x64-mingw-debug-headless and -release
  windows-msvc   windows-x64-msvc-debug-headless and -release

Each step is a CMake preset (CMakePresets.json), configured, built and tested: its work
in build/<platform>/<variant>/, what it makes in out/<platform>/<variant>/; the haxe
steps are the binding's own scripts. --only takes step names (linux-x64-debug-headless,
wasm32-release, haxe, ...). Stops at the first step that fails.
"""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import builds  # noqa: E402


WEB = {'wasm32-release': ['--backend=webgl2'], 'wasm32-release-threads': ['--backend=webgl2', '--threads'],
       'wasm32-release-webgpu-threads': ['--backend=webgpu', '--threads']}
REMOTE = {'windows-mingw': [], 'windows-msvc': ['--msvc']}
HAXE = {'haxe': [['bindings/haxe/test/check.py']],
        'haxe-web': [['bindings/haxe/examples/build.py', 'web'], ['bindings/haxe/examples/build.py', 'drive']]}


def steps(web, windows):
    yield builds.native('release'), False
    yield builds.native('debug-headless'), True
    if builds.HOST in ('linux-x64', 'macos-arm64'):
        yield builds.native('debug-tsan'), True
        if shutil.which('x86_64-w64-mingw32-gcc'):
            yield 'windows-x64-mingw-release', False
            import run_wine
            if run_wine.find_wine():
                yield 'windows-x64-mingw-debug-headless', True
            else:
                print('verify: no Wine, so the Windows build is built but not run')
    has_haxe = shutil.which('haxe') is not None
    if has_haxe:
        yield 'haxe', False
    else:
        print('verify: no Haxe, so the Haxe binding is not checked')
    if web:
        for preset in WEB:
            yield preset, False
        if has_haxe:
            yield 'haxe-web', False
    if windows:
        for step in REMOTE:
            yield step, False


def run(*cmd):
    print('$ ' + ' '.join(cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT).returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--web', action='store_true', help='the web builds too, checked in a browser')
    ap.add_argument('--windows', metavar='HOST', help='also build and test on this Windows machine over ssh')
    ap.add_argument('--only', help='comma-separated steps to run')
    args = ap.parse_args()
    only = set(args.only.split(',')) if args.only else None

    web = args.web or (only is not None and any(s in WEB or s == 'haxe-web' for s in only))
    if only and any(s in REMOTE for s in only) and not args.windows:
        sys.exit('verify: the windows-* steps need --windows HOST')
    for preset, test in steps(web, args.windows):
        if only and preset not in only:
            continue
        print(f'== {preset}', flush=True)
        start = time.monotonic()
        if preset in REMOTE:
            ok = run(sys.executable, 'tools/run_remote_windows.py', args.windows, *REMOTE[preset])
        elif preset in HAXE:
            ok = all(run(sys.executable, *cmd) for cmd in HAXE[preset])
        else:
            ok = (run('cmake', '--preset', preset) and run('cmake', '--build', '--preset', preset)
                  and (not test or run('ctest', '--preset', preset))
                  and (preset not in WEB or run(sys.executable, 'tools/check_web.py', *WEB[preset])))
        if not ok:
            sys.exit(f'verify: FAIL at {preset}')
        print(f'== {preset}: ok ({time.monotonic() - start:.0f}s)', flush=True)
    print('verify: PASS')


if __name__ == '__main__':
    main()
