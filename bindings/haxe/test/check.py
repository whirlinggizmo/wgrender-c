#!/usr/bin/env python3
"""Run the binding against headless wgrender and assert what it gets back.

    test/check.py
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

Then check-cppia: the whole binding in a -D scriptable executable, and a cppia module
that calls every public function of it, loaded into that, which is what hot reload
does with an application's code (README, "Calling it from cppia").

And it compiles the binding's own C, host/wgr_guest.c, with wgrender's warnings as
errors, which no build of the binding otherwise holds it to.

Then it builds the suite twice: native, where it runs, and against the js binding,
where there is no host loop but a wrapper that exists only on hxcpp fails here rather
than in whichever example first happened to call it.
"""
import os
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'tools'))
from wgrpath import WGRENDER, host_os  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--lists',), positional=0)
from guestbuild import check_library, desktop_variant  # noqa: E402
ROOT = pathlib.Path(__file__).resolve().parent.parent
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
        run([sys.executable, ROOT / 'tools' / tool, '--check'])

    # wgrender's sources compiled in, as every build of the binding does
    # (project/Build.xml), with its headless flags
    # (found through haxelib, so it has to be this copy)
    print('check-bindings (headless)')
    check_library()
    BUILD.mkdir(parents=True, exist_ok=True)
    common = ['-D', 'wgr-headless', '-D', 'HXCPP_M64',
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

    check_cppia()

    print("host/wgr_guest.c with wgrender's warnings, as errors")
    check_guest_warnings()

    print('WebHost lists')
    return check_webhost_lists()


def check_cppia():
    """The whole binding, called from a cppia module (README, "Calling it from cppia").

    Hot reload runs an application's code as a cppia module, which calls the binding's
    compiled copies in the executable. Two ways the binding can be wrong for that
    compile and run natively: a function with a C type in its signature in a public
    class, which fails the C++ of a -D scriptable build (hxcpp makes a Dynamic wrapper
    for every static of one); and a wrapper without a compiled copy (`extern inline`,
    which every `overload` is) that makes the C call itself, which fails when a module
    that reaches it is built or loaded. So: an executable with the whole binding in it,
    built -D scriptable; and a module with a generated class that calls every public
    function of the binding once, loaded into it, so each one links or doesn't.
    """
    print('check-cppia: the whole binding, called from a cppia module')
    cppia = BUILD / 'cppia'
    cppia.mkdir(parents=True, exist_ok=True)
    base = ['-D', 'wgr-headless', '-D', 'HXCPP_M64',
            '-lib', 'wgrender-hx', '-cp', ROOT / 'test', '-dce', 'no',
            '--macro', "include('wgr', true, ['wgr.macros'])"]
    run([HAXE, *base, '--main', 'CppiaHost', '--cpp', cppia, '-D', 'HAXE_OUTPUT_FILE=check-cppia',
         '-D', 'scriptable', '-D', f'dll_export={cppia / "host.info"}'], cwd=ROOT)
    run([HAXE, *base, '--main', 'CppiaModule', '--cppia', cppia / 'reach.cppia',
         '-D', f'dll_import={cppia / "host.info"}',
         '--macro', 'wgr.macros.Cppia.callHost()', '--macro', 'wgr.macros.Cppia.reachAll()'], cwd=ROOT)
    run([cppia / ('check-cppia' + ('.exe' if os.name == 'nt' else '')), cppia / 'reach.cppia'])


def check_guest_warnings():
    """The binding's own C -- host/wgr_guest.c, the only C here that isn't wgrender's --
    compiled with the warnings wgrender's own build uses, as errors.

    hxcpp compiles it with the compiler's defaults and Emscripten's, so nothing else here
    ever holds it to them. The standard and flags come from wgrender's build.json, so
    this follows wgrender; -O2, because some warnings (a string that could be cut short)
    only appear once the optimizer traces values. On MSVC, /W3 /WX, the level wgrender's
    own MSVC build has.
    """
    import json
    import shutil
    manifest = json.loads((WGRENDER / 'build.json').read_text(encoding='utf-8'))
    BUILD.mkdir(parents=True, exist_ok=True)
    source, includes = ROOT / 'host/wgr_guest.c', [WGRENDER / 'include', ROOT / 'host']
    if os.name == 'nt':
        if not shutil.which('cl'):
            sys.exit('check: no cl.exe on PATH to compile host/wgr_guest.c with (run vcvars64.bat first)')
        run(['cl', '/nologo', '/c', '/W3', '/WX', '/O2', *(f'/I{d}' for d in includes),
             f'/Fo{BUILD / "wgr_guest.obj"}', source])
    else:
        cc = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:
            sys.exit('check: no C compiler (CC, cc, gcc or clang) to compile host/wgr_guest.c with')
        run([cc, f'-std={manifest["std"]}', '-O2', *manifest['warn'], '-Werror',
             *(f'-I{d}' for d in includes), '-c', '-o', BUILD / 'wgr_guest.o', source])


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
