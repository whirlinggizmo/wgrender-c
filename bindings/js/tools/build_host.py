#!/usr/bin/env python3
"""Build the JS binding's web site: wgrender as a wasm host, the binding, and the examples.

    bindings/js/tools/build_host.py [--webgpu] [--debug] [--full | --trimmed LISTING]

Writes out/wasm32/<variant>/site/js/ (wasm32-release's by default), beside the C
examples' site, so tools/serve_site.py serves it with /assets/ mounted:

    wgrender-host.js, .wasm   the host: wgrender's web library and the guest ABI glue
                              (bindings/host), linked with every call the binding makes
    wgrender.js, .d.ts, src/  the binding
    examples/<name>/          each example's page and script

Two modes, as the Haxe binding's host has them:

    --full     the default: the host exports every function in wgrender.exports.json
               (the generator's list), so any program on the binding runs against it,
               and wgrender.js is the whole binding. For development, and what a
               hot-reloading guest needs: reloaded code may call what the first build
               didn't.
    --trimmed LISTING
               the host exports only the functions LISTING names (one per line or
               comma-separated, with or without the leading _), and wgrender.js is
               trimmed to the same (gen_binding.py --trim). About half the size: for
               release. LISTING is what the program calls. For a plain JS program it
               comes from something that parses it, such as a bundler that reports which
               exports it kept (Rollup's renderedExports); nothing here derives it yet.

Runs tools/build_web_library.py first, which builds the library from build.json when
it's out of date. Needs emcc (exactly build.json's Emscripten).
"""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

WGRENDER = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(WGRENDER / 'tools'))
import builds  # noqa: E402
import cli  # noqa: E402
import headers  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--webgpu', '--debug', '--full', '--trimmed'), positional=None)

BINDING = WGRENDER / 'bindings/js'
HOST = WGRENDER / 'bindings/host'


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def main():
    backend = 'webgpu' if '--webgpu' in sys.argv else 'webgl2'
    debug = '--debug' in sys.argv
    preset = builds.web(backend=backend, debug=debug)
    _, variant = builds.split(preset)
    site = builds.programs(preset) / 'js'
    work = builds.work(preset) / 'js'
    work.mkdir(parents=True, exist_ok=True)

    run([sys.executable, WGRENDER / 'tools/build_web_library.py', f'BACKEND={backend}', 'WEB_THREADS=0',
         f'WEB_DEBUG={1 if debug else 0}'])
    target = json.loads((WGRENDER / 'build.json').read_text(encoding='utf-8'))['web'][variant]
    lib = builds.lib(preset) / 'libwgrender.a'

    rest = [a for a in sys.argv[1:] if not a.startswith('-')]
    trimmed = '--trimmed' in sys.argv
    if trimmed and '--full' in sys.argv:
        sys.exit('build_host: --full or --trimmed, not both')
    if trimmed != (len(rest) == 1) or len(rest) > 1:
        sys.exit('build_host: --trimmed takes one LISTING file, and nothing else takes an argument')
    # what the binding calls, its runtime's C library functions and runtime methods
    binding = json.loads((BINDING / 'wgrender.exports.json').read_text(encoding='utf-8'))
    if trimmed:
        listing = pathlib.Path(rest[0])
        listed = sorted({'_' + n.strip().lstrip('_') for n in re.split(r'[,\s]+', listing.read_text()) if n.strip()})
    else:
        listed = binding['functions']
    guest = sorted('_' + n for n in headers.functions_in(HOST / 'wgr_guest.h', WGRENDER, tool='build_host'))
    # guest.js's start() reads the host's version before anything else, so even a
    # trimmed host keeps those three
    version = ['_wgr_version_major', '_wgr_version_minor', '_wgr_version_patch']
    exports = ['_main'] + guest + sorted(set(listed) | set(version)) + binding['library']
    (work / 'exports.txt').write_text('\n'.join(exports) + '\n', encoding='utf-8')

    site.mkdir(parents=True, exist_ok=True)
    emcc = shutil.which('emcc') or sys.exit('build_host: no emcc on PATH (source emsdk_env first)')
    run([emcc, '-O2', f'-I{WGRENDER / "include"}', f'-I{HOST}', *target['program_cflags'],
         HOST / 'wgr_guest.c', lib, *target['ldflags'],
         '-sALLOW_TABLE_GROWTH=1', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createWgrHost',
         f'-sEXPORTED_RUNTIME_METHODS={",".join(binding["runtime"])}',
         f'-sEXPORTED_FUNCTIONS=@{work / "exports.txt"}',
         '-o', site / 'wgrender-host.js'])

    if trimmed:
        run([sys.executable, BINDING / 'tools/gen_binding.py', '--trim', listing, site])
    else:
        shutil.copy2(BINDING / 'wgrender.js', site / 'wgrender.js')
    shutil.copy2(BINDING / 'wgrender.d.ts', site / 'wgrender.d.ts')
    shutil.copytree(BINDING / 'src', site / 'src', dirs_exist_ok=True)
    examples = sorted(p for p in (BINDING / 'examples').iterdir() if p.is_dir())
    for example in examples:
        shutil.copytree(example, site / 'examples' / example.name, dirs_exist_ok=True)
    print(f'build_host: {"trimmed" if trimmed else "full"}, {len(exports)} exports, {len(examples)} examples '
          f'-> {os.path.relpath(site, WGRENDER)}')


if __name__ == '__main__':
    main()
