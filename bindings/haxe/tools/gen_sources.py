#!/usr/bin/env python3
"""Write project/wgrender.xml: wgrender's C sources and its flags, for hxcpp to compile.

    tools/gen_sources.py [WGRENDER_DIR]         write it
    tools/gen_sources.py --check [WGRENDER_DIR]  fail if it has drifted

Every build of wgrender-hx compiles wgrender from source rather than linking a prebuilt
library, which is what librl-hx does for librl: hxcpp uses MSVC on Windows by default,
MinGW objects and MSVC's linker cannot be mixed, and compiling from source means both
halves come from whatever toolchain hxcpp chose, on every platform, with nothing to
build first.

That is only possible because wgrender asks for nothing at build time: its three
shader headers are committed, its dependencies are vendored, and Python is needed to
*regenerate* shaders, not to compile. The build is a C compiler and these files.

The list is generated for the usual reason: hand-listing 47 files is a second build
to keep in step with the first, and --check fails when wgrender gains or loses one.
It comes from wgrender's build.json, its build as data: the sources, the vendored
single-header libraries' directories, and each build's defines, compile flags and link
flags (desktop per OS, headless, and the web without threads).
"""
import json
import os
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wgrpath import find  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--check',), positional=1)

ROOT = pathlib.Path(__file__).resolve().parent.parent
WGRENDER = find()
OUT = ROOT / 'project/wgrender.xml'

# The vendored single-header libraries (build.json's system_include). wgrender reaches
# them with -isystem to keep their warnings out of its -Wall -Wextra; hxcpp passes
# neither, so a plain include path is equivalent here and works in both flag syntaxes.
def vendored(manifest):
    return [d.split('/', 1)[1] for d in manifest['system_include'] if d.startswith('deps/')]


OSES = ['linux', 'macos', 'windows']  # hxcpp's defines for them, and build.json's names
WEB_BACKENDS = ['webgl2', 'webgpu']       # -D wgr-webgpu picks the second


def compile_flags(manifest):
    """wgrender's own compile flags for each build hxcpp can make, as nested sections:
    native (per OS, or headless with -D wgr-headless) and the web (hxcpp's emscripten
    target, always without threads; webgpu with -D wgr-webgpu, debug with --debug)."""
    desktop, web = manifest['desktop'], manifest['web']

    def defines(target, indent):
        return '\n'.join(f'{indent}<compilerflag value="-D{d}" />' for d in desktop[target]['defines'])

    def flags(values, indent):
        return '\n'.join(f'{indent}<compilerflag value="{v}" />' for v in values)

    native = '\n'.join(
        f'                <section if="{os}">\n{defines(os, " " * 20)}\n                </section>'
        for os in OSES)
    headless = '\n'.join(
        f'                <section if="{os}">\n{defines(os + "-headless", " " * 20)}\n                </section>'
        for os in OSES)
    webs = []
    for backend in WEB_BACKENDS:
        cond = 'if="wgr_webgpu"' if backend == 'webgpu' else 'unless="wgr_webgpu"'
        release, debug = web[f'{backend}-nothreads']['cflags'], web[f'{backend}-nothreads-debug']['cflags']
        webs.append(f'''            <section {cond}>
                <section unless="debug">
{flags(release, " " * 20)}
                </section>
                <section if="debug">
{flags(debug, " " * 20)}
                </section>
            </section>''')
    return native, headless, '\n'.join(webs)


def link_flags(manifest):
    """The platform libraries and link flags, per build, as compile_flags picks them.
    Flag syntax follows the toolchain rather than the platform: hxcpp defines `windows`
    for an MSVC build too, and link.exe answers -lopengl32 with an error."""
    desktop, web = manifest['desktop'], manifest['web']

    def libs(target, indent):
        spec = desktop[target]
        if target.startswith('windows'):
            gcc = '\n'.join(f'{indent}    <lib name="-l{l}" />' for l in spec['libs'])
            msvc = '\n'.join(f'{indent}    <lib name="{l}.lib" />' for l in spec['libs'])
            return (f'{indent}<section if="mingw">\n{gcc}\n{indent}</section>\n'
                    f'{indent}<section unless="mingw">\n{msvc}\n{indent}</section>')
        lines = [f'{indent}<lib name="-l{l}" />' for l in spec['libs']]
        for fw in spec.get('frameworks', []):
            lines += [f'{indent}<flag value="-framework" />', f'{indent}<flag value="{fw}" />']
        return '\n'.join(lines)

    def per_os(suffix):
        return '\n'.join(
            f'                <section if="{os}">\n{libs(os + suffix, " " * 20)}\n                </section>'
            for os in OSES)

    webs = []
    for backend in WEB_BACKENDS:
        cond = 'if="wgr_webgpu"' if backend == 'webgpu' else 'unless="wgr_webgpu"'
        each = lambda values: '\n'.join(f'                    <flag value="{v}" />' for v in values)
        webs.append(f'''            <section {cond}>
                <section unless="debug">
{each(web[f"{backend}-nothreads"]["ldflags"])}
                </section>
                <section if="debug">
{each(web[f"{backend}-nothreads-debug"]["ldflags"])}
                </section>
            </section>''')
    return per_os(''), per_os('-headless'), '\n'.join(webs)


def build(manifest, sources, deps):
    includes = ['include', 'src'] + [f'deps/{d}' for d in deps]
    isystem = '\n'.join(
        f'        <compilerflag value="-I${{WGRENDER}}/{i}" />' for i in includes)
    files = '\n'.join(f'        <file name="${{WGRENDER}}/src/{s}" />' for s in sources)
    native, headless, web = compile_flags(manifest)
    native_libs, headless_libs, web_links = link_flags(manifest)
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<!--
  Generated by tools/gen_sources.py from wgrender's build.json. Do not edit: run the tool.

  wgrender's {len(sources)} C sources, compiled by hxcpp with whatever toolchain it chose
  (gcc or clang, MinGW, MSVC, or emcc for the web), and the flags wgrender's own builds
  use for each: no prebuilt library, and nothing to build first. Every build of this
  binding comes through here (project/Build.xml includes it).

  Which build: native by default, per OS; -D wgr-headless for wgrender's headless
  build (no window, GPU or audio); hxcpp's emscripten target for the web, without
  threads (hxcpp's has none), webgl2 or, with -D wgr-webgpu, webgpu, and debug with
  hxcpp's own debug build. hxcpp's conditions test whether a define exists, and Haxe
  hands it -D wgr-webgpu as wgr_webgpu.
-->
<xml>
    <pragma once="true" />

    <files id="wgrender">
{isystem}

        <!-- One flag syntax for every compiler: cl.exe takes -D and -I as readily
             as /D and /I. -->
        <section unless="emscripten">
            <section unless="wgr_headless">
{native}
            </section>
            <section if="wgr_headless">
{headless}
            </section>
        </section>
        <section if="emscripten">
{web}
        </section>

        <!-- wgrender is C11 (_Static_assert, among other things). MSVC wants /std:c11,
             which VS 2019 16.8 added, and everyone else wants -std=gnu11. hxcpp sets
             toolchain=msvc rather than `msvc`, so MSVC is spelled "windows and not
             mingw", which is true of exactly that combination. -->
        <compilerflag value="/std:c11" if="windows" unless="mingw" />
        <compilerflag value="-std=gnu11" unless="windows" />
        <compilerflag value="-std=gnu11" if="mingw" />

{files}
    </files>

    <!-- wgrender as a static library, linked as one: the program keeps only the parts
         of it that it reaches (and on the web, only their JS glue), as it would
         linking wgrender's own prebuilt library, where the objects linked directly
         would all be kept. hxcpp's own static linker (ar, emar or lib.exe), into its
         build directory. -->
    <target id="wgrender-lib" output="wgrender" tool="linker" toolid="static_link">
        <files id="wgrender" />
    </target>

    <target id="haxe">
        <target id="wgrender-lib" />
        <lib name="wgrender${{LIBEXT}}" />
        <section unless="emscripten">
            <section unless="wgr_headless">
{native_libs}
            </section>
            <section if="wgr_headless">
{headless_libs}
            </section>
        </section>
        <section if="emscripten">
{web_links}
        </section>
    </target>
</xml>
'''


def main():
    path = WGRENDER / 'build.json'
    if not path.exists():
        sys.exit(f'no {path}: this wgrender predates it; update the submodule')
    manifest = json.loads(path.read_text(encoding='utf-8'))
    sources = sorted(s.split('/', 1)[1] for s in manifest['sources'])
    deps = vendored(manifest)
    text = build(manifest, sources, deps)
    if '--check' in sys.argv:
        current = OUT.read_text(encoding='utf-8') if OUT.exists() else ''
        if current != text:
            print(f'project/wgrender.xml is stale: wgrender has {len(sources)} sources\n'
                  '  run tools/gen_sources.py')
            return 1
        print(f'sources: current ({len(sources)} files, {len(deps)} vendored)')
        return 0
    for comment in re.findall(r'<!--(.*?)-->', text, re.S):
        if '--' in comment:
            sys.exit('a generated XML comment contains "--", which XML forbids:\n'
                     f'  {comment.strip()[:100]}')
    OUT.write_text(text, encoding='utf-8')
    print(f'project/wgrender.xml  {len(sources)} sources, {len(deps)} vendored deps')
    return 0


if __name__ == '__main__':
    sys.exit(main())
