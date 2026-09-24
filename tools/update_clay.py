#!/usr/bin/env python3
"""Update the vendored Clay in deps/clay from libwgrender's Clay fork.

    tools/update_clay.py [ref]     ref: a branch, tag or commit (default: main)

The fork (CLAY_REPO, default github.com/robknopf/clay) is nicbarker/clay plus fixes
libwgrender needs, each on its own branch merged into the fork's main; sync it with
upstream there, then run this. Clay is used only by examples/clay.c. The files are
copied at their upstream paths, so the demo layout's own include of "../../clay.h"
resolves unchanged. deps/clay/VERSION records the fork commit and the upstream commit
it's based on. Review the diff, rebuild (tools/verify.py --web), then commit.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = os.environ.get('CLAY_REPO', 'https://github.com/robknopf/clay.git')
UPSTREAM = 'https://github.com/nicbarker/clay.git'
DEST = ROOT / 'deps' / 'clay'
SRC = ROOT / 'build' / 'clay-src'
FILES = ['clay.h', 'LICENSE.md', 'examples/shared-layouts/clay-video-demo.c']


def git(*args, capture=False):
    done = subprocess.run(['git', '-C', str(SRC), *args], check=True, text=True,
                          capture_output=capture)
    return done.stdout.strip() if capture else None


def main():
    ref = sys.argv[1] if len(sys.argv) > 1 else 'main'
    if not (SRC / '.git').is_dir():
        subprocess.run(['git', 'clone', '--quiet', REPO, str(SRC)], check=True)
    git('fetch', '--quiet', REPO)
    git('fetch', '--quiet', UPSTREAM, 'main')
    upstream_head = git('rev-parse', 'FETCH_HEAD', capture=True)
    git('fetch', '--quiet', REPO, ref)
    git('checkout', '--quiet', '--detach', 'FETCH_HEAD')
    commit = git('rev-parse', 'HEAD', capture=True)
    date = git('log', '-1', '--format=%cs', 'HEAD', capture=True)
    base = git('merge-base', 'HEAD', upstream_head, capture=True)

    for name in FILES:
        if not (SRC / name).is_file():
            sys.exit(f'update_clay: {name} is no longer in clay')
        (DEST / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(SRC / name, DEST / name)

    (DEST / 'VERSION').write_text('\n'.join([
        "# Vendored from libwgrender's fork of nicbarker/clay (zlib license, LICENSE.md): a C99 UI",
        "# layout library (flexbox-like; emits render commands and draws nothing itself), with",
        "# fixes libwgrender needs on top of upstream. Used only by examples/clay.c, which draws it",
        "# through libwgrender's public API; libwgrender itself doesn't include or link Clay",
        "# (docs/PLAN-ui.md). Files keep their upstream paths; update them all together with",
        "# tools/update_clay.py.",
        f'clay-fork {REPO} {commit} {date}',
        f'clay-upstream {UPSTREAM} {base}']) + '\n')

    print(f'update_clay: deps/clay now at fork {commit} ({date}), upstream base {base}')
    subprocess.run(['git', '-C', str(ROOT), 'status', '--short', '--', 'deps/clay'])


if __name__ == '__main__':
    main()
