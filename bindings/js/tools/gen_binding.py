#!/usr/bin/env python3
"""Generate the JS binding -- wgrender.js, wgrender.d.ts and wgrender.exports.json --
from wgrender's public headers.

    bindings/js/tools/gen_binding.py          write all three
    bindings/js/tools/gen_binding.py --check  say whether they are current, and exit
                                              non-zero if not
    bindings/js/tools/gen_binding.py --only NAMES OUT
                                              write OUT/wgrender.js with only the functions
                                              NAMES lists (one per line or comma-separated,
                                              with or without the leading _, as a host's
                                              export list has them): a program's own binding

The binding is plain JavaScript with TypeScript declarations beside it, so a JS program
uses it as it is and a TypeScript one gets every type and the header's doc comment for
each call. Every C function keeps its C name: the header is the contract and its docs
are the binding's docs, so the names read the same in both.

What crosses the boundary, and how (the runtime that does it is src/runtime.js):

  numbers, handles, enums  passed as they are; unsigned results come back unsigned
  bool                     true/false in, true/false out
  const char *             copied into the wasm stack in, read back as a string out
  vec2_t, vec3_t, records  read out of one fixed slot into a fresh plain object
  wgr_keyboard_state_t     too big to copy each frame: a pointer, read through the
                           generated WGR_KEYBOARD_STATE layout

Every name that crosses into the host (an export, HEAPF32, stackAlloc) or into an
object a caller passed (into["x"]) is a quoted key, so a property-mangling minifier
can't break it; V8 compiles a constant quoted key as a dotted one, so it costs nothing.

Calls that take a C callback (wgr_set_init and the rest) aren't here: a JS guest
registers its ops through src/guest.js and the guest ABI (bindings/host) instead.
Anything else with no rendering is listed when the tool runs, never dropped silently.

wgrender.exports.json lists the wasm exports the binding calls, for tools/build_host.py
to link the host with: data the generator writes, so nothing reads the JS back.
"""
import hashlib
import json
import re
import pathlib
import subprocess
import sys

WGRENDER = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(WGRENDER / 'tools'))
import cli  # noqa: E402
import headers  # noqa: E402  (the headers as clang reads them)
from cabi import layout  # noqa: E402  (the structs' wasm32 layout)

if __name__ == '__main__':
    cli.parse(__doc__, ('--check', '--only'), positional=None)

OUT = WGRENDER / 'bindings/js'
FILES = ('wgrender.js', 'wgrender.d.ts', 'wgrender.exports.json')

# C callback typedefs: replaced by the guest ABI for a JS guest.
CALLBACKS = {'wgr_lifecycle_fn', 'wgr_frame_fn', 'wgr_tick_fn'}

# Records returned by value but too big to copy per frame: a pointer, and their layout.
OPAQUE = {'wgr_keyboard_state_t'}

# C type -> (TypeScript type, how a result comes back). 'raw' returns the number as it
# is; 'u32' makes it unsigned (a handle or a color has its top bit set as often as not).
SCALARS = {
    'void': ('void', None),
    'bool': ('boolean', 'bool'),
    'int': ('number', 'raw'),
    'unsigned int': ('number', 'u32'),
    'unsigned': ('number', 'u32'),
    'float': ('number', 'raw'),
    'double': ('number', 'raw'),
    'wgr_handle_t': ('wgr_handle_t', 'u32'),
    'wgr_color_t': ('wgr_color_t', 'u32'),
    'const char *': ('string', 'str'),
    'char *': ('string', 'str'),
}

# Names a parameter can't have in JS.
RESERVED = {'break', 'case', 'catch', 'class', 'const', 'continue', 'debugger', 'default', 'delete', 'do',
            'else', 'enum', 'export', 'extends', 'false', 'finally', 'for', 'function', 'if', 'import', 'in',
            'instanceof', 'new', 'null', 'return', 'super', 'switch', 'this', 'throw', 'true', 'try',
            'typeof', 'var', 'void', 'while', 'with', 'yield', 'let', 'static', 'await'}
# ...nor one of the names the generated code itself uses: a C parameter called `host`
# (wgr_asset_set_host's) would shadow the module every call goes through.
RESERVED |= {'host', 'cstr', 'str', 'record', 'opaqueSlot', 'out', 'into', 'mark', 'result', 'readI32'}

DIGEST_TAG = '// wgrender-headers: '


def param(name):
    return name + '_' if name in RESERVED else name


def header_digest():
    """A digest of wgrender's public headers: it moves when any of them does."""
    digest = hashlib.sha256()
    found = sorted((WGRENDER / 'include').glob('*.h'))
    for h in found:
        digest.update(h.name.encode())
        digest.update(h.read_bytes().replace(b'\r\n', b'\n'))  # the same on a CRLF checkout
    return digest.hexdigest()[:16], len(found)


def provenance(api):
    digest, count = header_digest()
    version = [str(api.values[f'WGR_VERSION_{part}']) for part in ('MAJOR', 'MINOR', 'PATCH')
               if f'WGR_VERSION_{part}' in api.values]
    try:
        commit = subprocess.run(['git', '-C', str(WGRENDER), 'describe', '--always', '--dirty'],
                                check=True, capture_output=True, text=True).stdout.strip()
    except Exception:
        commit = 'unknown'
    return version, commit, digest, count


def banner(version, commit, digest, count):
    return (f'// GENERATED by bindings/js/tools/gen_binding.py from wgrender\'s include/*.h -- do not edit.\n'
            f'// Re-run the tool when wgrender\'s API moves; it writes this file whole.\n'
            f'//\n'
            f'// Every name that crosses into the wasm host, or into an object a caller passed, is a\n'
            f'// quoted key -- host["_wgr_model_get_position"], host["HEAPF32"], into["x"] -- not a\n'
            f'// dotted one. A minifier that mangles property names leaves quoted keys alone, and the\n'
            f'// host\'s export names can\'t be renamed to match: dotted, `host._wgr_...` breaks under\n'
            f'// such a minifier (measured: esbuild --mangle-props=^_). It costs nothing: V8 compiles\n'
            f'// a constant quoted key exactly as a dotted one (measured: 0.757 ns per access either\n'
            f'// way), and a minifier that isn\'t mangling turns it back into the dotted form.\n'
            f'//\n'
            f'// wgrender {".".join(version) or "?"} at {commit}, {count} headers.\n'
            f'{DIGEST_TAG}{digest}\n')


def doc(text, indent=''):
    """A header comment as a JSDoc block, or nothing."""
    text = text.strip()
    if not text:
        return ''
    lines = [indent + '/**'] + [indent + ' * ' + line if line else indent + ' *'
                                for line in text.replace('*/', '* /').splitlines()] + [indent + ' */']
    return '\n'.join(lines) + '\n'


class Gen:
    def __init__(self, api):
        self.api = api
        self.enums = {name for name in api.enums if name.startswith('wgr_')}
        self.structs = {name: [(f.type, f.name, f.count) for f in s.fields] for name, s in api.structs.items()}
        self.records = set()   # structs returned by value as objects
        self.opaque = set()    # structs returned as a pointer and a layout
        self.js, self.dts, self.exports, self.skipped = [], [], [], []

    # ------------------------------------------------------------- types ---

    def ts(self, ctype):
        """The TypeScript type for a C type, or None if there's no rendering."""
        if ctype in SCALARS:
            return SCALARS[ctype][0]
        if ctype in self.enums:
            return ctype
        if ctype in self.structs and ctype not in OPAQUE:
            return ctype
        return None

    def read_record(self, ctype, base):
        """A JS expression building `ctype`'s object from the heap at byte address `base`."""
        fields, _ = layout(self.structs, ctype)
        parts = []
        for field, ftype, at, count in fields:
            if ftype in self.structs:
                value = self.read_record(ftype, f'{base} + {at}')
                self.records.add(ftype)
            elif count:
                value = '[' + ', '.join(self.read_scalar(ftype, f'{base} + {at + i * 4}') for i in range(count)) + ']'
            else:
                value = self.read_scalar(ftype, f'{base} + {at}')
            parts.append(f'"{field}": {value}')
        return '{ ' + ', '.join(parts) + ' }'

    def read_scalar(self, ctype, address):
        if ctype == 'float':
            return f'host["HEAPF32"][({address}) >> 2]'
        if ctype == 'bool':
            return f'host["HEAPU8"][{address}] !== 0'
        if ctype in ('unsigned int', 'unsigned', 'wgr_handle_t', 'wgr_color_t'):
            return f'host["HEAPU32"][({address}) >> 2]'
        return f'host["HEAP32"][({address}) >> 2]'

    # --------------------------------------------------------- functions ---

    def function(self, fn):
        name, ret = fn.name, fn.returns
        if fn.variadic:
            self.skipped.append((name, 'variadic')); return
        params = [(p.type, param(p.name)) for p in fn.params]
        types = [ret] + [t for t, _ in params]
        if any(t in CALLBACKS for t in types):
            self.skipped.append((name, 'takes a C callback (a JS guest uses the guest ABI)')); return
        # A struct parameter would cross by a hidden pointer on wasm; the API rules pass
        # vectors as their components, so none exists, and one that appears is refused.
        bad = ([t for t, _ in params if self.ts(t) is None or t in self.structs]
               + ([] if ret in OPAQUE or self.ts(ret) else [ret]))
        if bad:
            self.skipped.append((name, f'no rendering for {bad[0]}')); return

        args = ', '.join(n for _, n in params)
        call_args = ', '.join(f'cstr({n})' if t in ('const char *', 'char *') else n for t, n in params)
        sig = ', '.join(f'{n}: {self.ts(t)}' for t, n in params)
        self.exports.append('_' + name)
        comment = doc(fn.doc)

        # A call that passes a string releases its stack space as it returns, so a frame
        # making thousands of them can't overflow the wasm stack (runtime.js).
        strings = any(t in ('const char *', 'char *') for t, _ in params)
        save = '    const mark = host["stackSave"]();\n' if strings else ''
        restore = '    host["stackRestore"](mark);\n' if strings else ''

        if ret in OPAQUE:
            self.opaque.add(ret)
            _, size = layout(self.structs, ret)
            self.js.append(f'{comment}export function {name}({args}) {{\n'
                           f'    const out = opaqueSlot("{name}", {size});\n{save}'
                           f'    host["_{name}"](out{", " if call_args else ""}{call_args});\n{restore}'
                           f'    return out;\n}}\n')
            self.dts.append(f'{comment}export declare function {name}({sig}): number;\n')
            return
        if ret in self.structs:
            self.records.add(ret)
            fields, size = layout(self.structs, ret)
            flat = all(ftype not in self.structs and not count for _, ftype, _, count in fields)
            if not flat:
                self.js.append(f'{comment}export function {name}({args}) {{\n'
                               f'    const out = record({size});\n{save}'
                               f'    host["_{name}"](out{", " if call_args else ""}{call_args});\n{restore}'
                               f'    return {self.read_record(ret, "out")};\n}}\n')
                self.dts.append(f'{comment}export declare function {name}({sig}): {ret};\n')
                return
            # A flat record (vec2_t, vec3_t, ...) can be read into an object the caller
            # owns: pass it last and it is filled and returned, so a getter in a hot loop
            # makes no garbage. Without it, a new object, as before. One name either way.
            # An array or typed array is filled by index (x, y, z in order), which no
            # minifier can rename, for a caller whose own field names get mangled.
            fill = ' '.join(f'into["{field}"] = {self.read_scalar(ftype, f"out + {at}")};' for field, ftype, at, _ in fields)
            fill_at = ' '.join(f'into[{i}] = {self.read_scalar(ftype, f"out + {at}")};' for i, (_, ftype, at, _) in enumerate(fields))
            self.js.append(f'{comment}export function {name}({args}{", " if args else ""}into) {{\n'
                           f'    const out = record({size});\n{save}'
                           f'    host["_{name}"](out{", " if call_args else ""}{call_args});\n{restore}'
                           f'    if (into) {{\n'
                           f'        if (Array.isArray(into) || ArrayBuffer.isView(into)) {{ {fill_at} }}\n'
                           f'        else {{ {fill} }}\n'
                           f'        return into;\n    }}\n'
                           f'    return {self.read_record(ret, "out")};\n}}\n')
            self.dts.append(f'{comment}export declare function {name}({sig}{", " if sig else ""}into?: Out<{ret}>): {ret};\n'
                            f'export declare function {name}({sig}{", " if sig else ""}into: number[] | Float32Array | Float64Array): number[] | Float32Array | Float64Array;\n')
            return

        call = f'host["_{name}"]({call_args})'
        how = SCALARS[ret][1] if ret in SCALARS else 'raw'   # an enum is a number
        if strings:
            value = {None: None, 'bool': 'result !== 0', 'raw': 'result', 'u32': 'result >>> 0',
                     'str': 'str(result)'}[how]
            body = (f'{save}    {"" if value is None else "const result = "}{call};\n{restore}'
                    + (f'    return {value};\n' if value else ''))
            self.js.append(f'{comment}export function {name}({args}) {{\n{body}}}\n')
        else:
            body = {None: f'{call};', 'bool': f'return {call} !== 0;', 'raw': f'return {call};',
                    'u32': f'return {call} >>> 0;', 'str': f'return str({call});'}[how]
            self.js.append(f'{comment}export function {name}({args}) {{\n    {body}\n}}\n')
        self.dts.append(f'{comment}export declare function {name}({sig}): {self.ts(ret)};\n')

    # ------------------------------------------------------------ output ---

    def enum_blocks(self):
        js, dts = [], []
        for name in sorted(self.enums):
            e = self.api.enums[name]
            js.append(doc(e.doc) + '\n'.join(f'export const {k} = {v};' for k, v in e.values.items()) + '\n')
            dts.append(doc(e.doc) + '\n'.join(f'export declare const {k}: {v};' for k, v in e.values.items())
                       + f'\nexport type {name} = ' + ' | '.join(f'typeof {k}' for k in e.values) + ';\n')
        return js, dts

    def record_types(self):
        out = []
        for name in sorted(self.records):
            fields, _ = layout(self.structs, name)
            members = []
            for field, ftype, _, count in fields:
                t = ftype if ftype in self.structs else ('boolean' if ftype == 'bool' else self.ts(ftype) or 'number')
                members.append(f'    readonly {field}: {"readonly " + t + "[]" if count else t};')
            out.append(doc(self.api.structs[name].doc) + f'export interface {name} {{\n' + '\n'.join(members) + '\n}\n')
        return out

    def layouts(self):
        """Where an opaque record's fields sit, in 32-bit words from its pointer."""
        js, dts = [], []
        for name in sorted(self.opaque):
            fields, size = layout(self.structs, name)
            const = name.upper().removesuffix('_T')
            entries = [f'BYTES: {size}'] + [f'{field}: {at // 4}' for field, _, at, _ in fields]
            js.append(f'/** Where `{name}`\'s fields sit, in 32-bit words from the pointer its getter returns\n'
                      f' * (an array field: its first element). Read with readI32(pointer, index). */\n'
                      f'export const {const} = Object.freeze({{ {", ".join(entries)} }});\n')
            dts.append(f'export declare const {const}: {{ readonly BYTES: number; '
                       + ' '.join(f'readonly {field}: number;' for field, _, _, _ in fields) + ' };\n')
        return js, dts


def defines(api):
    """The WGR_ defines that are integer constants (colors, window flags, limits, the
    version), as clang evaluates them (tools/headers.py's values). A define that isn't
    one (a version string) is listed, not carried."""
    js, dts, skipped = [], [], []
    for name in api.defines:
        if not name.startswith('WGR_'):
            continue
        if name not in api.values:
            skipped.append((name, 'a define that is not an integer constant'))
            continue
        number = api.values[name]
        js.append(f'export const {name} = {f"0x{number:X}" if number > 0xFF else number};')
        dts.append(f'export declare const {name}: {number};')
    return '\n'.join(js) + '\n', '\n'.join(dts) + '\n', skipped


def generate(only=None):
    """The three files; `only`, a set of function names, keeps just those functions."""
    api = headers.read(WGRENDER, tool='gen_binding')
    gen = Gen(api)
    for fn in api.functions.values():
        if only is None or fn.name in only:
            gen.function(fn)
    version, commit, digest, count = provenance(api)
    head = banner(version, commit, digest, count)
    major, minor, patch = (version + ['0', '0', '0'])[:3]
    built = (f'/** The wgrender this binding was generated from; guest.start() compares it with the host\'s. */\n'
             f'export const BUILT_VERSION = Object.freeze({{ major: {major}, minor: {minor}, patch: {patch}, '
             f'commit: "{commit}", headers: "{digest}" }});\n')
    enum_js, enum_dts = gen.enum_blocks()
    define_js, define_dts, define_skipped = defines(api)
    gen.skipped += define_skipped
    layout_js, layout_dts = gen.layouts()

    js = (head + '\nimport { host, cstr, str, record, opaqueSlot } from "./src/runtime.js";\n'
          'export { readI32 } from "./src/runtime.js";\n\n'
          + built + '\n' + define_js + '\n' + '\n'.join(enum_js) + '\n' + '\n'.join(layout_js) + '\n' + '\n'.join(gen.js))
    dts = (head + '\nexport type wgr_handle_t = number;\n'
           '/** A record a getter can fill instead of making a new one: its fields, writable. */\n'
           'export type Out<T> = { -readonly [K in keyof T]: T[K] };\n'
           '/** Packed 8-bit RGBA, 0xRRGGBBAA: a value, not a handle (include/wgr_types.h). */\nexport type wgr_color_t = number;\n\n'
           'export declare const BUILT_VERSION: { readonly major: number; readonly minor: number; '
           'readonly patch: number; readonly commit: string; readonly headers: string };\n'
           '/** A 32-bit word at `index` words from `pointer`, for the opaque layouts. */\n'
           'export declare function readI32(pointer: number, index: number): number;\n\n'
           + '\n'.join(gen.record_types()) + '\n' + define_dts + '\n' + '\n'.join(enum_dts) + '\n' + '\n'.join(layout_dts) + '\n'
           + '\n'.join(gen.dts))
    exports = json.dumps({'generated': head.splitlines()[0][3:], 'headers': digest,
                          'functions': gen.exports}, indent=1) + '\n'
    return {'wgrender.js': js, 'wgrender.d.ts': dts, 'wgrender.exports.json': exports}, gen, digest


def check():
    digest, count = header_digest()
    stale = []
    for name in FILES:
        path = OUT / name
        if not path.exists():
            stale.append(f'{name}: missing'); continue
        text = path.read_text(encoding='utf-8')
        if (DIGEST_TAG + digest) not in text and f'"headers": "{digest}"' not in text:
            stale.append(f'{name}: generated from other headers than these ({digest})')
    if stale:
        print('gen_binding: the JS binding is STALE')
        for s in stale:
            print(f'  {s}')
        print('  run bindings/js/tools/gen_binding.py')
        return 1
    print(f'gen_binding: the JS binding is current ({count} headers, {digest})')
    return 0


def main():
    if '--check' in sys.argv:
        sys.exit(check())
    if '--only' in sys.argv:
        rest = [a for a in sys.argv[1:] if not a.startswith('-')]
        if len(rest) != 2:
            sys.exit('gen_binding: --only takes a names file and an output directory')
        wanted = {n.strip().lstrip('_') for n in re.split(r'[,\s]+', pathlib.Path(rest[0]).read_text()) if n.strip()}
        files, gen, _ = generate(wanted)
        out = pathlib.Path(rest[1])
        out.mkdir(parents=True, exist_ok=True)
        (out / 'wgrender.js').write_text(files['wgrender.js'], encoding='utf-8')
        print(f'gen_binding: {len(gen.exports)} of the functions -> {out / "wgrender.js"}')
        return
    files, gen, _ = generate()
    for name, text in files.items():
        (OUT / name).write_text(text, encoding='utf-8')
    print(f'wgrender.js / wgrender.d.ts: {len(gen.exports)} functions, {len(gen.enums)} enums, '
          f'{len(gen.records)} records, {len(gen.opaque)} opaque')
    if gen.skipped:
        by = {}
        for name, why in gen.skipped:
            by.setdefault(why, []).append(name)
        print(f'not bound ({len(gen.skipped)}):')
        for why, names in sorted(by.items(), key=lambda kv: -len(kv[1])):
            print(f'  {len(names):>3}  {why}: {", ".join(sorted(names))}')


if __name__ == '__main__':
    main()
