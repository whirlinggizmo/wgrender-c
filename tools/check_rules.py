#!/usr/bin/env python3
"""wgrender's guardrails: the conventions a compiler doesn't check, checked through tools
that parse what they check, never by scanning text.

    tools/check_rules.py [--lib path/to/libwgrender.a]

  naming    AGENTS.md § Naming: wgr_<noun>_t struct types (no _data_t / _instance_t); a
            pointer resolved from a handle is <noun>_ptr; the public API is handle-only;
            wgr_ is public and wgri_ internal, and every public macro carries the prefix
  manifest  build.json's sources are src/*.c, no more and no fewer
  values    AGENTS.md § Public API shape: a struct in a public signature is one of the
            fixed-layout math values (VALUE_STRUCTS), or a record RECORDS_TODO lists
  getters   AGENTS.md § Public API shape: every value a setter stores has a getter,
            unless GETTERS_EXEMPT says why not, or GETTERS_TODO lists it as a known gap
  modules   the core never calls an optional subsystem by name (src/internal/
            wgri_module.h), read from the library's symbol table: needs --lib and nm,
            and is skipped without them. ctest's `check` passes the headless library.
  tools     AGENTS.md § Naming: every tool is named verb first (TOOL_VERBS), and run
            with --help prints its usage and exits 0 having done nothing, and with an
            argument it doesn't take exits non-zero. A tool is every .py in the tool
            folders (TOOL_FILES) but the modules tools import (TOOL_MODULES).

The public headers come from clang (tools/headers.py); src/ from clang's AST of each
source file, limited to the declarations named wgr*: every wgr_ and wgri_ function and
type (-ast-dump-filter, which keeps all of src/ to a second or two). Backend-free
headers and examples aren't here: the build compiles each public header alone, and the
examples, with no backend on the path. Needs clang (emsdk's, or one on PATH).

Exits non-zero on any violation. ctest runs it as the test `check`.
"""
import argparse
import collections
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import headers  # noqa: E402  (the public headers as clang reads them)


def files(pattern):
    return sorted(ROOT.glob(pattern))


def src_declarations(clang):
    """Every declaration in src/ whose name has wgr in it, from clang's AST of each source
    file, compiled as compile_flags.txt says (clangd's flags): [(node, file)], the file
    each came from. -ast-dump-filter prints one JSON object per match."""
    from concurrent.futures import ThreadPoolExecutor
    flags = [line for line in (ROOT / 'compile_flags.txt').read_text(encoding='utf-8').splitlines() if line.strip()]

    def dump(source):
        done = subprocess.run([clang, '-fsyntax-only', '-Xclang', '-ast-dump=json', '-Xclang',
                               '-ast-dump-filter=wgr', *flags, str(source.relative_to(ROOT))],
                              cwd=ROOT, capture_output=True, text=True)
        if done.returncode != 0:
            first = next((line for line in done.stderr.splitlines() if 'error' in line), done.stderr.strip())
            sys.exit(f'check_rules: clang could not parse {source}\n  {first}')
        decoder, text, at, out = json.JSONDecoder(), done.stdout, 0, []
        while at < len(text):
            while at < len(text) and text[at].isspace():
                at += 1
            if at < len(text):
                node, at = decoder.raw_decode(text, at)
                headers._walk_files(node, str(source.relative_to(ROOT)))
                out.append(node)
        return out

    with ThreadPoolExecutor(8) as pool:
        return [node for nodes in pool.map(dump, files('src/*.c')) for node in nodes]


def walk(node):
    yield node
    for kid in node.get('inner') or ():
        yield from walk(kid)


def under(node, folder):
    f = node.get('_file') or ''
    return Path(f).as_posix().startswith(folder)


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


# Build flags the build passes with -D (WGR_HEADLESS, WGR_EXPORT_FULL_API): -D and
# #ifdef spell them alike, so they keep the public prefix wherever they're used.
BUILD_FLAGS = {'WGR_HEADLESS', 'WGR_EXPORT_FULL_API'}

# Public macros without the prefix, each a known gap: the check fails when one is gone
# and still listed.
MACROS_TODO = {'log_debug', 'log_error', 'log_fatal', 'log_info', 'log_trace', 'log_warn'}


def check_naming(r, api, src):
    types = sorted({n['name'] for d in src for n in walk(d)
                    if n.get('kind') in ('RecordDecl', 'TypedefDecl') and n.get('name')
                    and n['name'].endswith(('_data_t', '_instance_t'))}
                   | {name for name in list(api.structs) + list(api.enums) + list(api.typedefs)
                      if name.endswith(('_data_t', '_instance_t'))})
    r.result(types, 'no _data_t / _instance_t struct types',
             'struct types must be wgr_<noun>_t (no _data_t / _instance_t):')

    # A variable of pointer type, initialized from resolve() or resolve_<x>(), in a wgr_ or
    # wgri_ function of src/
    resolved, bad = 0, []
    for d in src:
        for fn in walk(d):
            if fn.get('kind') != 'FunctionDecl' or not under(fn, 'src/'):
                continue
            for v in walk(fn):
                if v.get('kind') != 'VarDecl' or not v.get('type', {}).get('qualType', '').endswith('*'):
                    continue
                callees = {n['referencedDecl'].get('name', '') for n in walk(v)
                           if n.get('kind') == 'DeclRefExpr' and n.get('referencedDecl', {}).get('kind') == 'FunctionDecl'}
                if any(c == 'resolve' or c.startswith('resolve_') for c in callees):
                    resolved += 1
                    if not v['name'].endswith('_ptr'):
                        bad.append(f'{fn["_file"]}: {fn["name"]}: {v["name"]}')
    r.result(sorted(set(bad)), f'resolved instance pointers use _ptr ({resolved} in wgr_/wgri_ functions)',
             'a pointer resolved from a handle must be named <noun>_ptr:')

    buffers = [f'{f.header}: {name}' for name, f in api.functions.items()
               if '_from_memory' in name or 'unsigned char *' in f.returns
               or any('unsigned char *' in p.type for p in f.params)]
    r.result(buffers, 'public API is handle-only (no byte buffers / *_from_memory)',
             'public API must be handle-only (no *_from_memory / raw byte buffers):')

    # One prefix per surface: include/ declares nothing wgri_, and src/internal/ declares
    # nothing wgr_ that include/ doesn't (it uses the public API; its own names are wgri_)
    public = (set(api.functions) | set(api.structs) | set(api.enums) | set(api.typedefs)
              | {c for e in api.enums.values() for c in e.values} | set(api.defines) | set(api.macros))
    r.result(sorted(n for n in public if n.lower().startswith('wgri_')), 'include/ declares no wgri_ symbol',
             'include/ must not declare internal wgri_ symbols:')
    internal = sorted({f'{n["_file"]}: {n["name"]}' for d in src for n in [d]
                       if under(n, 'src/internal/') and n.get('name', '').lower().startswith('wgr_')
                       and n['name'] not in public and n['name'] not in BUILD_FLAGS})
    r.result(internal, 'src/internal/ declares only wgr_ symbols include/ declares',
             "src/internal/ declares these wgr_ symbols, which include/ doesn't "
             '(internal symbols are wgri_; promote it to include/ if it should be public):')
    unprefixed = sorted(n for n in set(api.defines) | set(api.macros)
                        if not n.lower().startswith('wgr_') and n not in MACROS_TODO)
    gone = [f'{n}: on MACROS_TODO, but include/ defines no such macro now; take it off'
            for n in sorted(MACROS_TODO - set(api.macros) - set(api.defines))]
    r.result(unprefixed + gone, f'every public macro is WGR_/wgr_ ({len(MACROS_TODO)} known gaps)',
             'a public macro without the prefix (every program that includes the header gets the name):')


def check_manifest(r):
    listed = set(json.loads((ROOT / 'build.json').read_text())['sources'])
    present = {p.relative_to(ROOT).as_posix() for p in files('src/*.c')}
    r.result([f'{s}: not in src/' for s in sorted(listed - present)]
             + [f'{s}: not in build.json' for s in sorted(present - listed)],
             "build.json's sources are src/*.c",
             "build.json's sources don't match src/*.c:")


# Structs a public call may take or return by value: math values whose layout can never
# change (AGENTS.md § Public API shape). matrix_t qualifies too, and joins the day a
# public call needs one.
VALUE_STRUCTS = {'vec2_t', 'vec3_t', 'vec4_t', 'quat_t'}

# Records returned by value today, each a known gap: per-field getters or a handle to
# the result replace it. The check fails when one leaves the API and is still listed.
RECORDS_TODO = {'wgr_pick_result_t', 'wgr_pick_stats_t', 'wgr_touch_t', 'wgr_touch_gesture_t',
                'wgr_mouse_state_t', 'wgr_keyboard_state_t'}


def check_values(r, api):
    """No struct crosses the API except a math value, or a record already on the list."""
    used = collections.defaultdict(set)
    for name, f in api.functions.items():
        for t in [f.returns] + [p.type for p in f.params]:
            base = t.replace('const ', '').replace('*', '').strip()
            if base in api.structs:
                used[base].add(name)
    bad = [f'{s}: in {", ".join(sorted(fns)[:3])}{" ..." if len(fns) > 3 else ""} -- a record; '
           'give it per-field getters or a handle' for s, fns in sorted(used.items())
           if s not in VALUE_STRUCTS and s not in RECORDS_TODO]
    gone = [f'{s}: on RECORDS_TODO, but no public call uses it now; take it off'
            for s in sorted(RECORDS_TODO - set(used))]
    r.result(bad + gone, f'structs in public calls are math values ({len(RECORDS_TODO)} records known)',
             'a public call takes or returns a record by value:')


# Setters whose values are read back by getters named otherwise: one getter per value,
# or a part getter the transform rule already requires.
GETTERS_PAIRED = {
    'wgr_light_set_shadow_bias': ('wgr_light_get_shadow_bias_constant', 'wgr_light_get_shadow_bias_slope'),
    'wgr_light_set_spot_cone': ('wgr_light_get_spot_inner_angle', 'wgr_light_get_spot_outer_angle'),
    'wgr_window_set_size': ('wgr_window_get_screen_size',),
}

# Setters with no getter on purpose, and why. A decision, not a backlog.
GETTERS_EXEMPT = {
    'wgr_asset_set_fetcher': 'a C callback and its void *: nothing a caller could use read back',
    'wgr_asset_set_manifest': 'an action: it reads a file; what it loads is not a value to return',
    **{f'wgr_shape2d_set_{g}': 'picks the geometry and its parameters in one call; '
       'reading it back would take a tagged union' for g in ('circle', 'line', 'rectangle')},
    **{f'wgr_shape3d_set_{g}': 'picks the geometry and its parameters in one call; '
       'reading it back would take a tagged union'
       for g in ('circle', 'cube', 'line', 'line_strip', 'rectangle', 'sphere')},
}

# Known gaps: each wants a getter, and gets one as its subsystem is next worked on.
# The check fails when one gains a getter and is still listed, so this only shrinks.
GETTERS_TODO = {
    'wgr_asset_set_upload_budget',
    'wgr_camera3d_set_view',
    *(f'wgr_emitter{d}_set_{v}' for d in ('2d', '3d') for v in (
        'alpha_mode', 'color', 'drag', 'frames', 'gravity', 'inherit_velocity', 'life', 'max',
        'rate', 'seed', 'size', 'source', 'spin', 'stretch', 'velocity', 'visible')),
    'wgr_emitter2d_set_spawn_box', 'wgr_emitter2d_set_spawn_circle',
    'wgr_emitter3d_set_spawn_box', 'wgr_emitter3d_set_spawn_sphere',
    'wgr_input_set_gamepad_deadzone',
    'wgr_logger_set_level',
    *(f'wgr_material_set_{v}' for v in (
        'color', 'float', 'int', 'texture', 'texture_sampling', 'vec2', 'vec3', 'vec4')),
    *(f'wgr_model_set_{v}' for v in (
        'animation', 'animation_loop', 'animation_speed', 'casts_shadow', 'mesh',
        'receives_shadow', 'tint')),
    *(f'wgr_scene_set_{v}' for v in (
        'active_camera', 'ambient', 'background', 'clip', 'environment', 'layer', 'tonemap')),
    'wgr_shape2d_set_color', 'wgr_shape2d_set_outline', 'wgr_shape3d_set_color',
    *(f'wgr_sound_set_{v}' for v in ('audio', 'loop', 'pan', 'pitch', 'volume')),
    *(f'wgr_sprite2d_set_{v}' for v in (
        'nine_slice', 'pick_alpha_test', 'size', 'source', 'texture', 'tint')),
    *(f'wgr_sprite3d_set_{v}' for v in (
        'extent', 'facing', 'pick_alpha_test', 'size', 'source', 'texture', 'tint')),
    *(f'wgr_text2d_set_{v}' for v in ('align', 'color', 'font', 'max_width', 'text')),
    *(f'wgr_text3d_set_{v}' for v in ('align', 'color', 'facing', 'font', 'max_width', 'text')),
    'wgr_texture_set_sampling',
    'wgr_window_set_title',
}


def check_getters(r, api):
    """Every value a setter stores has a getter (AGENTS.md § Public API shape)."""
    public = set(api.functions)
    missing, rotted = [], []
    for setter in sorted(public):
        kind, sep, value = setter.partition('_set_')
        if not sep or kind.count('_') != 1:
            continue
        if value == 'transform':
            paired = (f'{kind}_get_position',)   # the transform rule: a getter per part
        else:
            paired = GETTERS_PAIRED.get(setter) or next(
                ((g,) for g in (f'{kind}_get_{value}', f'{kind}_is_{value}', f'{kind}_has_{value}')
                 if g in public), ())
        has = bool(paired) and all(g in public for g in paired)
        if setter in GETTERS_TODO and has:
            rotted.append(f'{setter}: has a getter now; take it off GETTERS_TODO')
        elif not has and setter not in GETTERS_EXEMPT and setter not in GETTERS_TODO:
            missing.append(f'{setter}: no {kind}_get_{value} (or is_/has_)')
    unknown = [f'{name}: listed, but include/ declares no such setter'
               for name in sorted((GETTERS_EXEMPT.keys() | GETTERS_TODO | GETTERS_PAIRED.keys()) - public)]
    r.result(missing + rotted + unknown,
             f'every stored value has a getter ({len(GETTERS_EXEMPT)} exempt, '
             f'{len(GETTERS_TODO)} known gaps)',
             'setters without a getter (add one, or say why in GETTERS_EXEMPT):')


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


# Where tools live, and the modules among them: imported by tools, never run. Every other
# file is a tool.
TOOL_FILES = ['tools/*.py', 'tools/bench/*.py', 'bindings/haxe/tools/*.py', 'bindings/haxe/test/check.py',
              'bindings/haxe/examples/build.py', 'bindings/haxe/examples/simple-hxcpp/build.py']
TOOL_MODULES = {'tools/builds.py', 'tools/cli.py', 'tools/headers.py', 'tools/hostcache.py', 'tools/weblib.py', 'tools/bench/measure.py',
                'bindings/haxe/tools/guestbuild.py', 'bindings/haxe/tools/members.py',
                'bindings/haxe/tools/wgrpath.py', 'bindings/haxe/tools/wgrweb.py'}
# What a tool's name may start with: what it does. A bare verb is a name too (serve.py).
TOOL_VERBS = ('build', 'check', 'compare', 'compress', 'create', 'drive', 'fetch', 'finish', 'gen', 'measure',
              'pack', 'run', 'serve', 'setup', 'show', 'update', 'verify', 'watch')
# Tools whose name is fixed by what runs them: a project's build script, a test suite.
TOOL_NAMES_FIXED = {'bindings/haxe/test/check.py', 'bindings/haxe/examples/build.py',
                    'bindings/haxe/examples/simple-hxcpp/build.py'}


def check_tools(r):
    from concurrent.futures import ThreadPoolExecutor
    tools = sorted({p.relative_to(ROOT).as_posix() for pattern in TOOL_FILES for p in ROOT.glob(pattern)}
                   - TOOL_MODULES)
    names = [t for t in tools if t not in TOOL_NAMES_FIXED
             and Path(t).stem.split('_')[0] not in TOOL_VERBS]
    r.result([f'{t}: not named verb first ({", ".join(TOOL_VERBS)})' for t in names],
             f'{len(tools)} tools named verb first', 'tools named for what they do')

    def answers(tool):
        def run(arg):
            return subprocess.run([sys.executable, str(ROOT / tool), arg], cwd=ROOT, capture_output=True,
                                  timeout=60).returncode
        problems = []
        if run('--help') != 0:
            problems.append(f'{tool}: --help exits non-zero')
        if run('--no-such-argument') == 0:
            problems.append(f'{tool}: an argument it does not take is accepted')
        return problems

    with ThreadPoolExecutor(8) as pool:
        hits = [p for problems in pool.map(answers, tools) for p in problems]
    r.result(hits, f'{len(tools)} tools answer --help and refuse an unknown argument',
             'every tool answers --help (exit 0, nothing done) and refuses what it does not take')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--lib', help='the library whose symbols the modules check reads')
    args = ap.parse_args()
    r = Report()
    clang = headers.require_clang('check_rules')
    api = headers.read(ROOT, clang, tool='check_rules')
    check_naming(r, api, src_declarations(clang))
    check_manifest(r)
    check_values(r, api)
    check_getters(r, api)
    check_modules(r, args.lib)
    check_tools(r)
    if r.failed:
        sys.exit(1)
    print('PASS')


if __name__ == '__main__':
    main()
