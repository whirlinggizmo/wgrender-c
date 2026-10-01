#!/usr/bin/env python3
"""Where wgrender is, for every tool here: the repository this binding lives in, and the
names of what the examples build.

    from wgrpath import WGRENDER, native_preset, web_variant

The binding is bindings/haxe in wgrender-c, so wgrender is two directories up, in a
checkout and in a `haxelib git ... bindings/haxe` install alike (that clones the
whole repository and makes bindings/haxe the library's root).

The examples' builds are named as wgrender's (its tools/builds.py): a preset
<platform>-<variant>, what it makes in out/<platform>/<variant>/ (programs in bin/, a
web build's site in site/), its work in build/<preset>/.
"""
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WGRENDER = ROOT.parent.parent

sys.path.insert(0, str(WGRENDER / 'tools'))
import builds  # noqa: E402,F401  (wgrender's: platform names, split, ...)


def native_platform():
    """hxcpp's native platform: this machine's (builds.HOST), and on Windows the
    toolchain hxcpp uses: MSVC by default, MinGW with HXCPP_MINGW."""
    if builds.HOST.startswith('windows-') and os.environ.get('HXCPP_MINGW'):
        return 'windows-x64-mingw'
    return builds.HOST


def native_preset(variant='release'):
    """An hxcpp native build's name: linux-x64-release, windows-x64-mingw-release, ..."""
    return f'{native_platform()}-{variant}'


def web_variant(hxcpp=False):
    """A web build's variant, from the settings wgrender's web build takes (BACKEND,
    WEB_THREADS, WEB_DEBUG): release, release-webgpu-threads, debug, ... A JS guest
    on a wasm host is the default; Haxe compiled into the wasm by hxcpp adds -hxcpp."""
    backend = os.environ.get('BACKEND') or 'webgl2'
    threads = not hxcpp and (os.environ.get('WEB_THREADS') or '0') == '1'
    debug = (os.environ.get('WEB_DEBUG') or '0') == '1'
    return builds.split(builds.web(backend, threads, debug))[1] + ('-hxcpp' if hxcpp else '')


def exe(name):
    """A program's file name on this machine."""
    return name + ('.exe' if os.name == 'nt' else '')
