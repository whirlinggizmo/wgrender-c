#!/usr/bin/env python3
"""wgrender's guardrails: the conventions a compiler doesn't check.

    tools/check.py [--lib path/to/libwgrender.a]

  backend   include/ and examples/ don't depend on sokol: no sokol #includes, no sokol
            API identifiers (the prose word "sokol" is fine)
  naming    AGENTS.md § Naming: wgr_<noun>_t struct types; a pointer resolved from a
            handle is <noun>_ptr; the public API is handle-only; wgr_ is public and
            wgri_ internal
  manifest  build.json's sources are src/*.c, no more and no fewer
  modules   the core never calls an optional subsystem by name (src/internal/
            wgri_module.h), read from the library's symbol table: needs --lib and nm,
            and is skipped without them. ctest's `check` passes the headless library.

Exits non-zero on any violation. ctest runs it as the test `check`.
"""
import argparse
import collections
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def files(pattern):
    return sorted(ROOT.glob(pattern))


def grep(pattern, paths):
    """`path:line: text` for every line of paths matching pattern."""
    rx = re.compile(pattern)
    hits = []
    for path in paths:
        for n, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
            if rx.search(line):
                hits.append(f'{path.relative_to(ROOT).as_posix()}:{n}: {line.strip()}')
    return hits


class Report:
    def __init__(self):
        self.failed = False

    def result(self, hits, ok, fail):
        if hits:
            self.failed = True
            print(f'FAIL: {fail}')
            for h in hits:
                print(f'  {h}')
        else:
            print(f'ok: {ok}')


def check_backend(r):
    token = (r'\b(sapp_|sgl_|sdtx_|sglue_|stm_|saudio_|sfetch_|sg_[a-zA-Z])|\b(SOKOL_|SAPP_)'
             r'|#\s*include\s*[<"]sokol')
    for d, glob in (('include', '*.h'), ('examples', '*.c')):
        r.result(grep(token, files(f'{d}/{glob}')), f'{d}/ is backend-free',
                 f'backend (sokol) symbols leaked into {d}/:')


def stripped(paths):
    """C headers without comments or #include lines: the names they declare and use."""
    text = '\n'.join(p.read_text(errors='replace') for p in paths)
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return re.sub(r'^\s*#\s*include[^\n]*$', '', text, flags=re.M)


def check_naming(r):
    ours = files('src/**/*.c') + files('src/**/*.h') + files('include/**/*.h')
    r.result(grep(r'\}\s*wgr_[a-z0-9_]+_(data|instance)_t\s*;', ours),
             'no _data_t / _instance_t struct types',
             'struct types must be wgr_<noun>_t (no _data_t / _instance_t):')

    resolved = grep(r'\*[A-Za-z_]\w*\s*=.*\bresolve(_[a-z]+)?\(', files('src/*.c'))
    r.result([h for h in resolved if not re.search(r'\*[A-Za-z_]\w*_ptr\s*=', h)],
             'resolved instance pointers use _ptr',
             'a pointer resolved from a handle must be named <noun>_ptr:')

    r.result(grep(r'_from_memory|\bunsigned char\s*\*', files('include/**/*.h')),
             'public API is handle-only (no byte buffers / *_from_memory)',
             'public API must be handle-only (no *_from_memory / raw byte buffers):')

    # One prefix per surface. Telling a declaration from a use needs a parser, so check
    # what doesn't: every wgr_ name an internal header mentions is one include/ declares
    # (it is using the public API), and include/ never mentions wgri_. Build flags the
    # build passes with -D (WGR_HEADLESS) are exempt: -D and #ifdef spell them alike.
    build_flags = {'WGR_HEADLESS'}
    public = stripped(files('include/*.h'))
    internal = stripped(files('src/internal/*.h'))
    public_names = set(re.findall(r'\b(?:wgr|WGR)_\w+\b', public))
    internal_public = sorted(set(re.findall(r'\b(?:wgr|WGR)_\w+\b', internal))
                             - public_names - build_flags)
    public_internal = sorted(set(re.findall(r'\b(?:wgri|WGRI)_\w+\b', public)))
    r.result(internal_public, 'src/internal/ names only wgr_ symbols include/ declares',
             "src/internal/ names these wgr_ symbols, which include/ doesn't declare "
             '(internal symbols are wgri_; promote it to include/ if it should be public):')
    r.result(public_internal, 'include/ mentions no wgri_ symbol',
             'include/ must not mention internal wgri_ symbols:')


def check_manifest(r):
    listed = set(json.loads((ROOT / 'build.json').read_text())['sources'])
    present = {p.relative_to(ROOT).as_posix() for p in files('src/*.c')}
    r.result([f'{s}: not in src/' for s in sorted(listed - present)]
             + [f'{s}: not in build.json' for s in sorted(present - listed)],
             "build.json's sources are src/*.c",
             "build.json's sources don't match src/*.c:")


def check_modules(r, lib):
    nm = shutil.which('nm') or shutil.which('llvm-nm')
    if not lib or not nm:
        print('skip: modules (needs --lib and nm)')
        return
    out = subprocess.run([nm, '-A', lib], capture_output=True, text=True).stdout
    defs, refs, optional = {}, collections.defaultdict(set), set()
    for line in out.splitlines():  # "lib.a:obj.o:ADDRESS TYPE NAME", or "lib.a:obj.o: U NAME"
        fields = line.split()
        if len(fields) < 3:
            continue
        obj, kind, name = fields[0].split(':')[-2], fields[-2], fields[-1]
        if kind == 'U':
            refs[obj].add(name)
        elif kind in 'TDRBW':
            defs.setdefault(name, obj)
        if name.startswith('wgri_register_') and name.endswith('_module'):
            optional.add(obj)
    bad = [f'{obj} -> {name} ({defs[name]})'
           for obj in sorted(set(refs) - optional)
           for name in sorted(refs[obj]) if defs.get(name) in optional]
    r.result(bad, f'core reaches the {len(optional)} optional subsystems only through modules and hooks',
             'core code calls optional subsystems by name (go through wgri_module.h and the hooks):')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--lib', help='the library whose symbols the modules check reads')
    args = ap.parse_args()
    r = Report()
    check_backend(r)
    check_naming(r)
    check_manifest(r)
    check_modules(r, args.lib)
    if r.failed:
        sys.exit(1)
    print('PASS')


if __name__ == '__main__':
    main()
