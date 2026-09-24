#!/usr/bin/env python3
"""Update the vendored sokol headers in deps/sokol from libwgrender's sokol fork.

    tools/update_sokol.py [ref]     ref: a branch, tag or commit (default: master)

The fork (SOKOL_REPO, default github.com/robknopf/sokol) is floooh/sokol plus fixes
libwgrender needs; sync it with upstream there, then run this. Only the headers already
in deps/sokol are copied. deps/sokol/VERSION records the fork commit and the upstream
commit it's based on. Review the diff, rebuild (tools/verify.py --web), then commit.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = os.environ.get('SOKOL_REPO', 'https://github.com/robknopf/sokol.git')
UPSTREAM = 'https://github.com/floooh/sokol.git'
DEST = ROOT / 'deps' / 'sokol'
SRC = ROOT / 'build' / 'sokol-src'


def git(*args, capture=False):
    done = subprocess.run(['git', '-C', str(SRC), *args], check=True, text=True,
                          capture_output=capture)
    return done.stdout.strip() if capture else None


def main():
    ref = sys.argv[1] if len(sys.argv) > 1 else 'master'
    if not (SRC / '.git').is_dir():
        subprocess.run(['git', 'clone', '--quiet', REPO, str(SRC)], check=True)
    git('fetch', '--quiet', 'origin')
    git('fetch', '--quiet', UPSTREAM, 'master')
    try:
        target = git('rev-parse', '--verify', f'origin/{ref}', capture=True)
    except subprocess.CalledProcessError:
        target = ref
    git('checkout', '--quiet', '--detach', target)
    commit = git('rev-parse', 'HEAD', capture=True)
    date = git('log', '-1', '--format=%cs', 'HEAD', capture=True)
    base = git('merge-base', 'HEAD', 'FETCH_HEAD', capture=True)

    for file in sorted([*DEST.glob('*.h'), *DEST.glob('util/*.h')]):
        name = file.relative_to(DEST)
        if not (SRC / name).is_file():
            sys.exit(f'update_sokol: {name.as_posix()} is no longer in sokol')
        shutil.copyfile(SRC / name, file)

    version = DEST / 'VERSION'
    shdc = [l for l in version.read_text().splitlines() if l.startswith('sokol-tools-bin ')]
    version.write_text('\n'.join([
        "# Vendored sokol headers, from libwgrender's fork of floooh/sokol (fixes libwgrender needs on",
        "# top of upstream). Only the files in this directory are vendored; update them all",
        "# together with tools/update_sokol.py.",
        f'sokol-fork {REPO} {commit} {date}',
        f'sokol-upstream {UPSTREAM} {base}',
        '# Shaders in src/shaders/*.glsl.h are generated with sokol-shdc from',
        '# https://github.com/floooh/sokol-tools-bin (tools/gen_shaders.py fetches it):',
        *shdc]) + '\n')

    # deps/sokol_utils reads sokol_app's private state: make sure it still compiles
    check = ROOT / 'build' / 'sokol-src-check'
    check.mkdir(parents=True, exist_ok=True)
    (check / 'utils.c').write_text('#define SOKOL_IMPL\n#define SOKOL_GLCORE\n#define SOKOL_NO_ENTRY\n'
                                   '#include "sokol_app.h"\n#include "sokol_app_utils.h"\n')
    cc = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
    if cc:
        done = subprocess.run([cc, '-std=gnu11', '-c', '-isystem', str(DEST), '-isystem',
                               str(ROOT / 'deps' / 'sokol_utils'), str(check / 'utils.c'),
                               '-o', str(check / 'utils.o')])
        if done.returncode != 0:
            sys.exit('update_sokol: deps/sokol_utils/sokol_app_utils.h no longer compiles against this '
                     'sokol; fix it (mark changes [libwgrender], list them in its VERSION)')
    else:
        print('update_sokol: no C compiler (CC, cc, gcc or clang), so sokol_app_utils.h is unchecked')

    print(f'update_sokol: deps/sokol now at fork {commit} ({date}), upstream base {base}')
    subprocess.run(['git', '-C', str(ROOT), 'diff', '--stat', '--', 'deps/sokol'])


if __name__ == '__main__':
    main()
