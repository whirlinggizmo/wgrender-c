#!/usr/bin/env python3
"""wgrender's web library, built with emcc alone: no make, no shell, on any OS.

    tools/buildweb.py [BACKEND=webgl2|webgpu] [WEB_THREADS=1|0] [WEB_DEBUG=0|1] [-j N]

The same settings as `make web`, spelled the same way (they also come from the
environment, then default as make's do), and the same result: build/<dir>/libwgrender.a,
<dir> being webgl2, webgl2-nothreads, webgpu-debug, ... `make web` runs this. It reads
how to compile from mk/build.json (tools/gen_manifest.py), which is what lets a binding
build wgrender for the web on a machine that has Emscripten and nothing else: emsdk
brings the Python this runs on.

Incremental, as make is: an object is rebuilt when it is missing, older than its
source or than any header its .d file names, or when the flags changed.
"""
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = {'BACKEND': 'webgl2', 'WEB_THREADS': '1', 'WEB_DEBUG': '0'}


def settings(args):
    """KEY=VALUE arguments, then the environment, then make's defaults."""
    given = dict(a.split('=', 1) for a in args if '=' in a)
    unknown = set(given) - set(DEFAULTS)
    if unknown:
        sys.exit(f'buildweb: unknown setting {", ".join(sorted(unknown))}\n{__doc__}')
    chosen = {k: given.get(k) or os.environ.get(k) or v for k, v in DEFAULTS.items()}
    if chosen['BACKEND'] not in ('webgl2', 'webgpu'):
        sys.exit(f"buildweb: BACKEND must be webgl2 or webgpu (got '{chosen['BACKEND']}')")
    for k in ('WEB_THREADS', 'WEB_DEBUG'):
        if chosen[k] not in ('0', '1'):
            sys.exit(f"buildweb: {k} must be 0 or 1 (got '{chosen[k]}')")
    return chosen


def web_dir(s):
    return (s['BACKEND'] + ('' if s['WEB_THREADS'] == '1' else '-nothreads')
            + ('-debug' if s['WEB_DEBUG'] == '1' else ''))


def tool(name):
    """emcc/emar as installed: emcc.bat on Windows, which PATHEXT lets which() find."""
    found = shutil.which(name)
    if not found:
        sys.exit(f'buildweb: no {name} on PATH (emsdk: emsdk activate, or emsdk_env)')
    return found


def depends(d_file):
    """The files a .d file says an object depends on (make's syntax: `obj: a b \\`)."""
    try:
        text = d_file.read_text()
    except OSError:
        return None
    text = text.replace('\\\n', ' ')
    deps = []
    for line in text.splitlines():
        # the target ends at the first ': ' (a Windows path has a drive colon, no space)
        if ': ' not in line:
            continue
        rest = line.split(': ', 1)[1]
        cur = ''
        for part in rest.split(' '):
            if part.endswith('\\'):  # an escaped space inside a path
                cur += part[:-1] + ' '
                continue
            cur += part
            if cur:
                deps.append(cur)
            cur = ''
    return deps


def stale(src, obj, d_file):
    if not obj.exists():
        return True
    built = obj.stat().st_mtime
    deps = depends(d_file)
    if deps is None:
        return True
    for dep in [src, *deps]:
        path = Path(dep) if Path(dep).is_absolute() else ROOT / dep
        if not path.exists() or path.stat().st_mtime > built:
            return True
    return False


def main():
    args = sys.argv[1:]
    jobs = os.cpu_count() or 4
    if '-j' in args:
        i = args.index('-j')
        jobs = int(args[i + 1])
        del args[i:i + 2]
    s = settings(args)
    manifest = json.loads((ROOT / 'mk/build.json').read_text())
    name = web_dir(s)
    target = manifest['web'][name]
    build = ROOT / 'build' / name
    obj_dir = build / 'obj'
    obj_dir.mkdir(parents=True, exist_ok=True)

    emcc, emar = tool('emcc'), tool('emar')
    flags = [f"-std={manifest['std']}", *manifest['warn'], *target['cflags'],
             *[f'-I{d}' for d in manifest['include']],
             *[x for d in manifest['system_include'] for x in ('-isystem', d)]]
    # flags changed: rebuild everything, as make's flags stamp does
    stamp = obj_dir / 'flags.buildweb'
    line = ' '.join(flags)
    if not stamp.exists() or stamp.read_text() != line:
        for o in obj_dir.glob('*.o'):
            o.unlink()
        stamp.write_text(line)

    jobs_to_run = []
    objects = []
    for src in manifest['sources']:
        obj = obj_dir / (Path(src).stem + '.o')
        objects.append(obj)
        if stale(ROOT / src, obj, obj.with_suffix('.d')):
            jobs_to_run.append((src, obj))

    def compile_one(job):
        src, obj = job
        cmd = [emcc, *flags, '-MD', '-MP', '-c', src, '-o', str(obj)]
        done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        return src, done

    failed = False
    if jobs_to_run:
        print(f'buildweb: {name}: compiling {len(jobs_to_run)} of {len(objects)}', flush=True)
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for src, done in pool.map(compile_one, jobs_to_run):
                if done.stdout.strip() or done.stderr.strip():
                    print(f'{src}:\n{done.stdout}{done.stderr}', end='', flush=True)
                if done.returncode != 0:
                    failed = True
    if failed:
        sys.exit('buildweb: compile failed')

    lib = build / 'libwgrender.a'
    if jobs_to_run or not lib.exists():
        if lib.exists():
            lib.unlink()  # rcs into an existing archive would keep members that are gone
        done = subprocess.run([emar, 'rcs', str(lib), *map(str, objects)], cwd=ROOT)
        if done.returncode != 0:
            sys.exit('buildweb: emar failed')
        print(f'built {lib.relative_to(ROOT).as_posix()}')
    else:
        print(f'{lib.relative_to(ROOT).as_posix()} is up to date')


if __name__ == '__main__':
    main()
