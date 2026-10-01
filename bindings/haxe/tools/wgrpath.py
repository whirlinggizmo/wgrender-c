#!/usr/bin/env python3
"""Where wgrender is, for every tool here: the repository this binding lives in.

    from wgrpath import WGRENDER

The binding is bindings/haxe in wgrender-c, so wgrender is two directories up, in a
checkout and in a `haxelib git ... bindings/haxe` install alike (that clones the
whole repository and makes bindings/haxe the library's root).
"""
import platform
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
WGRENDER = ROOT.parent.parent


def host_os():
    """This OS's name in build output directories: linux, macos, windows.

    The examples' out/<platform>/<variant>/ directories use it, as wgrender's own builds
    do (out/linux/release, out/web/webgl2-nothreads, ...).

    "desktop" still means native-not-web everywhere it is prose or a define; it is
    only the directories that name the OS.
    """
    system = platform.system()
    return {'Linux': 'linux', 'Darwin': 'macos', 'Windows': 'windows'}.get(system, system.lower())
