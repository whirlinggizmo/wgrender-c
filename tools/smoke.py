#!/usr/bin/env python3
"""Desktop smoke test: run headless example binaries for a fixed number of frames and
fail on a non-zero exit, a timeout, or error-level logs ([ERROR], [FATAL], sokol panics).

    tools/smoke.py [--frames N] [--runner CMD] <binary>...

ctest runs it once per example (smoke.<name>); run by hand, the binaries run in
parallel and are reported in argument order. --runner runs each binary under a
command (tools/wine.py for a Windows build made on Linux).

Runs from the repository root, so the examples find examples/assets. Headless builds
pace frames at 60 per second (so timing-driven code runs as in a real game): 180
frames is about 3 seconds each.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROBLEM = re.compile(r'\[(ERROR|FATAL)|\[panic\]|ABORTING')


def run(binary, frames, runner, timeout):
    env = dict(os.environ, WGR_HEADLESS_FRAMES=str(frames))
    start = time.monotonic()
    try:
        done = subprocess.run([*runner, str(Path(binary).resolve())], cwd=ROOT, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=timeout)
        rc, log = done.returncode, done.stdout
    except subprocess.TimeoutExpired as e:
        rc, log = None, e.stdout or b''
    return rc, log.decode(errors='replace'), time.monotonic() - start


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--frames', type=int, default=180)
    ap.add_argument('--runner', default='', help='a command to run each binary under')
    ap.add_argument('binaries', nargs='+')
    args = ap.parse_args()
    runner = shlex.split(args.runner)
    timeout = args.frames / 60 + 20

    n = len(args.binaries)
    if n > 1:
        print(f'smoke: {n} examples, {args.frames} frames each (headless, in parallel)')
    with ThreadPoolExecutor(max_workers=n) as pool:
        results = list(pool.map(lambda b: run(b, args.frames, runner, timeout), args.binaries))

    failed = 0
    for binary, (rc, log, secs) in zip(args.binaries, results):
        name = Path(binary).stem
        problems = [line for line in log.splitlines() if PROBLEM.search(line)]
        if rc is None:
            why = f'timed out after {timeout:.0f}s'
        elif rc != 0:
            why = f'exit {rc}, {secs:.1f}s'
        elif problems:
            why = f'error logs, {secs:.1f}s'
        else:
            print(f'  ok    {name} ({secs:.1f}s)')
            continue
        failed += 1
        print(f'  FAIL  {name} ({why})')
        for line in problems or log.splitlines()[-5:]:
            print(f'          {line}')

    if failed:
        print(f'FAIL: {failed} of {n} example(s)')
        sys.exit(1)
    if n > 1:
        print(f'PASS: {n} examples')


if __name__ == '__main__':
    main()
