#!/usr/bin/env python3
"""Where wgrender is, answered the same way by every tool here.

    from wgrpath import find
    WGRENDER = find()            # honours argv and $WGRENDER_DIR
    WGRENDER = find(explicit)    # or a path the caller already has

In order:

  1. a path given on the command line, or $WGRENDER_DIR
  2. ../wgrender-c beside this repo -- a checkout, working against its own wgrender
  3. project/lib/wgrender-c -- the vendored submodule, which is what an install has

Eight tools used to guess at this, with seven different answers: most assumed the
sibling checkout, gen_sources assumed the submodule, and coverage.py ignored
WGRENDER_DIR altogether. Building an example out of a `haxelib git` install found it,
because there the sibling does not exist and every tool but one looked there anyway.
"""
import os
import platform
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def candidates():
    return [ROOT / '../wgrender-c', ROOT / 'project/lib/wgrender-c']


def find(explicit=None, argv=None):
    """The wgrender directory, or exit saying where it looked."""
    given = explicit or next(
        (a for a in (argv if argv is not None else sys.argv[1:]) if not a.startswith('--')), None)
    given = given or os.environ.get('WGRENDER_DIR')
    if given:
        path = pathlib.Path(given).resolve()
        if not (path / 'include/wgr.h').exists():
            sys.exit(f'no wgrender at {path} (no include/wgr.h)')
        return path
    for path in candidates():
        if (path / 'include/wgr.h').exists():
            return path.resolve()
    looked = '\n'.join(f'  {p.resolve()}' for p in candidates())
    sys.exit('cannot find wgrender. Looked in:\n' + looked
             + '\n\nSet WGRENDER_DIR, or run `haxelib run wgrender-hx setup` to fetch '
               'the submodule.')


def host_os():
    """This OS's name in build output directories: linux, macos, windows.

    The examples' out/<platform>/<variant>/ directories use it, as wgrender's own builds
    do (out/linux/release, out/web/webgl2-nothreads, ...).

    "desktop" still means native-not-web everywhere it is prose or a define; it is
    only the directories that name the OS.
    """
    system = platform.system()
    return {'Linux': 'linux', 'Darwin': 'macos', 'Windows': 'windows'}.get(system, system.lower())
