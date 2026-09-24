#!/usr/bin/env python3
"""mk/build.json: how wgrender builds, as data, for tools that build it without make.

    tools/gen_manifest.py           write mk/build.json from the Makefiles
    tools/gen_manifest.py --check   fail if mk/build.json is not what the Makefiles say
                                (make check runs this)

The Makefiles stay the one description of the build; this asks them (make's
print-var-NAME, for each target's settings) and writes the answer down, so a binding
can compile wgrender with its own toolchain (Nim's {.compile.}, hxcpp's build tool, a
Python loop over emcc) on a machine with no make and no POSIX shell, which is most
Windows machines. It is checked in and checked for drift like any generated file:
running this needs make, reading its output never does.

What it holds:

  sources         every library source, relative to wgrender's root
  include         -I directories; system_include: -isystem ones (the vendored
                  single-header libraries, whose warnings aren't ours)
  std, opt, warn  the C standard, and the optimization level and warning flags the
                  desktop builds use (warn and opt are gcc/clang's spelling)
  desktop         per OS (linux, macos, windows) and headless variant: defines, and
                  what a program links: libs by name (-l<name> for gcc and clang,
                  <name>.lib for MSVC), frameworks on macOS
  web             per build directory (webgl2, webgl2-nothreads, webgpu, ..., and
                  -debug): the full compile flags for the library (cflags), the ones a
                  program compiling against it must match (program_cflags), and the
                  link flags (ldflags). Emscripten only, so they are emcc's as they are.
"""
import json
import pathlib
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'mk/build.json'
SCHEMA = 1


def var(name, *settings, examples=False):
    """A Makefile variable, as make computes it for these settings."""
    cmd = ['make', '-s', '--no-print-directory', *(['-C', 'examples'] if examples else []),
           f'print-var-{name}', *settings]
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f'manifest: {" ".join(cmd)} failed:\n{out.stderr}')
    return shlex.split(out.stdout.strip())


def defines(flags):
    return [f[2:] for f in flags if f.startswith('-D')]


def libs(flags):
    return [f[2:] for f in flags if f.startswith('-l')]


def frameworks(flags):
    return [flags[i + 1] for i, f in enumerate(flags) if f == '-framework']


def build():
    incs = var('INCS')
    include = [incs[i][2:] for i in range(len(incs)) if incs[i].startswith('-I')]
    system_include = [incs[i + 1] for i in range(len(incs)) if incs[i] == '-isystem']

    desktop = {}
    for os_name, uname, extra in (('linux', 'Linux', []), ('macos', 'Darwin', []),
                                  ('windows', 'Linux', ['WINDOWS=1'])):
        for headless in (False, True):
            settings = [f'UNAME_S={uname}', *extra, *(['HEADLESS=1'] if headless else [])]
            link = var('LDLIBS_HEADLESS' if headless else 'LDLIBS_PLATFORM', *settings, examples=True)
            entry = {'defines': defines(var('DEFS', *settings)), 'libs': libs(link)}
            if frameworks(link):
                entry['frameworks'] = frameworks(link)
            if '-static' in link:
                entry['static'] = True  # MinGW: link its runtime statically
            desktop[os_name + ('-headless' if headless else '')] = entry

    web = {}
    for backend in ('webgl2', 'webgpu'):
        for threads in ('1', '0'):
            for debug in ('0', '1'):
                s = ['WEB=1', f'BACKEND={backend}', f'WEB_THREADS={threads}', f'WEB_DEBUG={debug}']
                web[var('WEB_DIR', *s)[0]] = {
                    'cflags': var('WEB_OPT', *s) + var('WASM_CFLAGS_BACKEND', *s),
                    'program_cflags': var('WASM_THREADS', *s),
                    'ldflags': var('WASM_LINK', *s),
                }

    return {
        'schema': SCHEMA,
        'generated_by': 'tools/gen_manifest.py, from the Makefiles: do not edit',
        'sources': sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / 'src').glob('*.c')),
        'include': include,
        'system_include': system_include,
        'std': var('STD')[0].removeprefix('-std='),
        'opt': var('OPT'),
        'warn': var('WARN'),
        'desktop': desktop,
        'web': web,
    }


def main():
    args = sys.argv[1:]
    if args not in ([], ['--check']):
        sys.exit(__doc__)
    text = json.dumps(build(), indent=2) + '\n'
    if args == ['--check']:
        current = MANIFEST.read_text() if MANIFEST.exists() else ''
        if current != text:
            sys.exit('manifest: mk/build.json is not what the Makefiles say; run tools/gen_manifest.py')
        print('manifest: mk/build.json is current')
        return
    MANIFEST.write_text(text)
    print(f'wrote {MANIFEST.relative_to(ROOT)}: {len(json.loads(text)["sources"])} sources, '
          f'{len(json.loads(text)["desktop"])} desktop and {len(json.loads(text)["web"])} web targets')


if __name__ == '__main__':
    main()
