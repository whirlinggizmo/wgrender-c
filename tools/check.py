#!/usr/bin/env python3
"""wgrender's guardrails: the conventions a compiler doesn't check.

    tools/check.py [--lib path/to/libwgrender.a]

  backend   include/ and examples/ don't depend on sokol: no sokol #includes, no sokol
            API identifiers (the prose word "sokol" is fine)
  naming    AGENTS.md § Naming: wgr_<noun>_t struct types; a pointer resolved from a
            handle is <noun>_ptr; the public API is handle-only; wgr_ is public and
            wgri_ internal
  manifest  build.json's sources are src/*.c, no more and no fewer
  values    AGENTS.md § Public API shape: a struct in a public signature is one of the
            fixed-layout math values (VALUE_STRUCTS), or a record RECORDS_TODO lists
  getters   AGENTS.md § Public API shape: every value a setter stores has a getter,
            unless GETTERS_EXEMPT says why not, or GETTERS_TODO lists it as a known gap
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
    # build passes with -D (WGR_HEADLESS, WGR_EXPORT_FULL_API) are exempt: -D and #ifdef
    # spell them alike.
    build_flags = {'WGR_HEADLESS', 'WGR_EXPORT_FULL_API'}
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


# Structs a public call may take or return by value: math values whose layout can never
# change (AGENTS.md § Public API shape). matrix_t qualifies too, and joins the day a
# public call needs one.
VALUE_STRUCTS = {'vec2_t', 'vec3_t', 'vec4_t', 'quat_t'}

# Records returned by value today, each a known gap: per-field getters or a handle to
# the result replace it. The check fails when one leaves the API and is still listed.
RECORDS_TODO = {'wgr_pick_result_t', 'wgr_pick_stats_t', 'wgr_touch_t', 'wgr_touch_gesture_t',
                'wgr_mouse_state_t', 'wgr_keyboard_state_t'}


def check_values(r):
    """No struct crosses the API except a math value, or a record already on the list."""
    text = stripped(files('include/*.h'))
    structs = set(re.findall(r'typedef\s+struct\s*\w*\s*\{[^{}]*\}\s*(\w+)\s*;', text, re.S))
    used = collections.defaultdict(set)
    for m in re.finditer(r'^\s*((?:const\s+)?\w+\s*\**)\s*(wgr_[a-z0-9_]+)\s*\(([^)]*)\)\s*;', text, re.M):
        for s in structs:
            if re.search(rf'\b{s}\b', m.group(1) + ' ' + m.group(3)):
                used[s].add(m.group(2))
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


def check_getters(r):
    """Every value a setter stores has a getter (AGENTS.md § Public API shape)."""
    public = set(re.findall(r'\b(wgr_[a-z0-9_]+)\s*\(', stripped(files('include/*.h'))))
    missing, rotted = [], []
    for setter in sorted(public):
        m = re.fullmatch(r'(wgr_[a-z0-9]+)_set_([a-z0-9_]+)', setter)
        if not m:
            continue
        kind, value = m.groups()
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--lib', help='the library whose symbols the modules check reads')
    args = ap.parse_args()
    r = Report()
    check_backend(r)
    check_naming(r)
    check_manifest(r)
    check_values(r)
    check_getters(r)
    check_modules(r, args.lib)
    if r.failed:
        sys.exit(1)
    print('PASS')


if __name__ == '__main__':
    main()
