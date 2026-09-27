#!/usr/bin/env python3
"""Run the binding against headless wgrender and assert what it gets back.

    test/check.py [WGRENDER_DIR]
    test/check.py --lists      only the WebHost list guards: pure Python, for CI

Headless is wgrender's own test build (-D wgr-headless: its headless flags, compiled in
by project/Build.xml as any build of the binding is): no window, GPU or audio, and
WGR_HEADLESS_FRAMES runs a fixed number of frames and returns, so the checks can assert
values rather than only compile. The work is in build/<os>/headless/.

This is the library's own test suite, not an example's. It lived in examples/simple-
hxcpp while the binding and that port grew together, which meant the thing that
decides whether wgrender-hx works was inside one of the things it was testing.

It runs three generators in --check mode first, because each guards a different way
the binding can be wrong without failing to compile:

  gen_raw    the C surface is generated from wgrender's headers; a stale one declares
             an API that no longer exists
  coverage   a stale omissions list hides a decision as a to-do
  refusals   wgrender's headers name every value a call refuses, and this fails if
             one of those sentences is not repeated in the binding's own docs
  keys       src/wgr/Key.hx is generated from wgr_keys.h, and this fails when a key
             moves; its static_asserts then fail the C++ build too
  sources    project/wgrender.xml lists wgrender's C files and flags for every build
             to compile with, and this fails when wgrender's build.json moves

Then it builds the suite twice: native, where it runs, and against the js binding,
where there is no host loop but a wrapper that exists only on hxcpp fails here rather
than in whichever example first happened to call it.
"""
import os
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'tools'))
from wgrpath import find, host_os  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--lists',), positional=1)
from guestbuild import check_library, desktop_variant  # noqa: E402
ROOT = pathlib.Path(__file__).resolve().parent.parent
WGRENDER = find()
HAXE = os.environ.get('HAXE', 'haxe')
# the repo's build/<os>/<variant>/ (the wg* layout): the headless variant of this host's
# native build, build/linux/headless or build/windows/msvc-headless
BUILD = ROOT / 'build' / host_os() / (desktop_variant().replace('release', '') + '-headless').lstrip('-')
FRAMES = os.environ.get('WGR_HEADLESS_FRAMES', '5')


def run(cmd, **kw):
    print('+', ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def main():
    for tool in ('gen_raw.py', 'gen_keys.py', 'coverage.py', 'refusals.py', 'gen_sources.py'):
        run([sys.executable, ROOT / 'tools' / tool, '--check', WGRENDER])

    # wgrender's sources compiled in, as every build of the binding does
    # (project/Build.xml), with its headless flags
    # (found through haxelib, so it has to be this copy)
    print('check-bindings (headless)')
    check_library()
    BUILD.mkdir(parents=True, exist_ok=True)
    common = ['-D', f'WGRENDER_DIR={WGRENDER}', '-D', 'wgr-headless', '-D', 'HXCPP_M64',
              '-lib', 'wgrender-hx', '-cp', ROOT / 'test', '--main', 'CheckBindings']
    run([HAXE, *common, '--cpp', BUILD / 'cpp', '-D', 'HAXE_OUTPUT_FILE=check-bindings', '-dce', 'full'],
        cwd=ROOT)

    binary = BUILD / 'cpp' / ('check-bindings' + ('.exe' if os.name == 'nt' else ''))
    # assets beside it, so the checks that load a real file can find one
    link = binary.parent / 'assets'
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(WGRENDER / 'examples/assets')
    run([binary], env={**os.environ, 'WGR_HEADLESS_FRAMES': FRAMES})

    # Everything above builds with -dce full, which is what hid a real bug: a
    # `static inline` that is only valid *because* it is inlined still gets an ordinary
    # method generated, and full DCE deleted that copy before anything checked it. A
    # debug build keeps it. Codegen only (-D no-compilation), so this costs seconds.
    print('code generation without DCE (what a debug build does)')
    run([HAXE, *common, '--cpp', BUILD / 'cpp-nodce', '-D', 'HAXE_OUTPUT_FILE=check-nodce',
         '-dce', 'no', '--debug', '-D', 'no-compilation'], cwd=ROOT)

    # -D wgr-listing makes wgr.macros.WebHost.build() return at once, as it does inside its own
    # listing compile, so this types the macro without making a host or spawning a child.
    print('type-check (js), with the WebHost macro')
    run([HAXE, '-cp', ROOT / 'src', '-cp', ROOT / 'test', '--main', 'CheckBindings',
         '--js', BUILD / 'check-js.js', '-D', 'js-es=6',
         '-D', 'wgr-listing', '--macro', 'wgr.macros.WebHost.build()'])

    print('WebHost lists')
    return check_webhost_lists()


def check_webhost_lists():
    """The two lists src/wgr/macros/WebHost.hx keeps by hand still match what they describe.

    Both are hand-kept on purpose -- the guest ABI is this binding's own contract, and
    what the JS reaches on the Emscripten module only exists inside function bodies --
    so this is what stops them drifting. One already had: the runtime list in the
    prototype carried HEAP8, which nothing uses. That direction is harmless; the other
    one is a runtime method the binding starts using that nobody adds, which fails in
    the browser as an undefined function.
    """
    import re
    web = (ROOT / 'src/wgr/macros/WebHost.hx').read_text(encoding='utf-8')

    def listed(name):
        m = re.search(name + r'\s*=\s*\[(.*?)\];', web, re.S)
        return set(re.findall(r'"(\w+)"', m.group(1))) if m else set()

    failed = False
    abi = set(re.findall(r'\b(wgr_guest_\w+)\s*\(', (ROOT / 'host/wgr_guest.h').read_text(encoding='utf-8')))
    have = listed('GUEST_ABI')
    for n in sorted(abi - have):
        print(f'  GUEST_ABI is missing {n}, which host/wgr_guest.h declares')
        failed = True
    for n in sorted(have - abi):
        print(f'  GUEST_ABI lists {n}, which host/wgr_guest.h does not declare')
        failed = True

    reached = set()
    for f in (ROOT / 'src/wgr').rglob('*.js.hx'):
        reached |= {n for n in re.findall(r'\bhost\.([A-Za-z_]\w*)', f.read_text(encoding='utf-8'))
                    if not n.startswith('_wgr_')}
    runtime = listed('RUNTIME_METHODS')
    for n in sorted(reached - runtime):
        print(f'  RUNTIME_METHODS is missing {n}, which src/wgr reaches on the host module')
        failed = True
    for n in sorted(runtime - reached):
        print(f'  RUNTIME_METHODS lists {n}, which nothing in src/wgr reaches')
        failed = True

    if failed:
        return 1
    print(f'  GUEST_ABI matches host/wgr_guest.h ({len(abi)}); '
          f'RUNTIME_METHODS matches src/wgr ({len(runtime)})')
    return 0


if __name__ == '__main__':
    sys.exit(check_webhost_lists() if '--lists' in sys.argv else main())
