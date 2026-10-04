#!/usr/bin/env python3
"""Generate the whole C surface — src/wgr/impl/Raw.cpp.hx and Raw.js.hx — from
wgrender's public headers.

    tools/gen_raw_externs.py          write both files
    tools/gen_raw_externs.py --check  say whether they are current, and exit non-zero if not

Both files are written whole; neither is ever patched. Run it when wgrender's API
moves, read what it reports, and rebuild.

Each file records the wgrender it came from — its version, its commit if there is one,
and a digest of the headers themselves — so `--check` can tell you the binding is
stale without regenerating it. The digest is what actually decides: a header edit
changes it whether or not anything was committed.

It reads `include/*.h` for functions, enums and structs, and needs help for exactly
four things, all declared in SPEC below rather than edited into the output:

  callbacks   a C function-pointer parameter has no shape the parser can infer
  values      a struct returned by value maps to a Haxe class in the public layer,
              whose constructor is expected to take the C fields in order
  opaque      a struct too big to copy per frame; JS gets a heap pointer (the JS
              binding's) and a generated table of field offsets to read it with
  skips       varargs, and anything else with no sane rendering

Raw.js.hx calls wgrender's JS binding (bindings/js/wgrender.js, generated from the same
headers by bindings/js/tools/gen_binding.py), which does the marshalling for every JS
guest; Raw.cpp.hx calls C directly. It reaches the binding through quoted keys
(WgrJs["wgr_..."], not WgrJs.wgr_...), so a property-mangling minifier can't break it;
V8 compiles a constant quoted key as a dotted one, so it costs nothing.

Everything else is mechanical. Functions whose types it cannot map are left out and
listed at the end, so the gap is reported rather than silent.
"""
import hashlib
import os
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wgrpath import WGRENDER  # noqa: E402
import headers  # noqa: E402  (wgrender's tools/headers.py: the headers as clang reads them)
from cabi import layout  # noqa: E402  (wgrender's tools/cabi.py: the structs' wasm32 layout)
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--check',), positional=0)

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'src/wgr/impl'

# ---------------------------------------------------------------- the spec ---

# C callback typedefs: the Haxe function type, and whether the JS layer can offer it.
# The guest ABI (bindings/host/wgr_guest.h) replaces every one of these on js, so none can.
CALLBACKS = {
    'wgr_lifecycle_fn': '(user:VoidStar) -> Void',
    'wgr_frame_fn': '(dt:Single, tickFraction:Single, user:VoidStar) -> Void',
    'wgr_tick_fn': '(dt:Single, user:VoidStar) -> Void',
}

# Structs returned by value, and the public-layer class the JS side builds from them.
# The constructor must take the C fields in order — that is what makes the read
# generatable rather than hand-written.
OPAQUE = {'wgr_keyboard_state_t'}

VALUES = {
    'vec2_t': 'Vec2',
    'vec3_t': 'Vec3',
    'wgr_mouse_state_t': 'MouseState',
    'wgr_pick_result_t': 'PickResult',
    'wgr_touch_t': 'Touch',
    'wgr_touch_gesture_t': 'TouchGesture',
    'wgr_pick_stats_t': 'PickStats',
}

# Vectors come back on js through one reused array (Raw.vector), which the public layer's
# Vec2.of / Vec3.of copy into a new vector or the caller's own.
VECTORS = {'vec2_t', 'vec3_t'}

# No sane rendering: varargs, or a pointer the public API rule says should not exist.
SKIP = set()

# Declarations the parser can't produce, rendered verbatim. Keep this short: anything
# here is a thing the generator does not know, and every entry is a thing to re-check
# when wgrender's API moves.
MANUAL_CPP = ''

MANUAL_JS = '''

	/** Fill wgrender's lifecycle slots with the guest glue's dispatchers. **/
	public static inline function wgr_guest_install():Void
		Raw.host["_wgr_guest_install"]();

'''

SCALARS = {  # C type -> (hxcpp, js), size
    'void': ('Void', 'Void', 0),
    'bool': ('Bool', 'Bool', 4),
    'int': ('Int', 'Int', 4),
    'unsigned int': ('UInt32', 'Int', 4),
    'unsigned': ('UInt32', 'Int', 4),
    'float': ('Single', 'Float', 4),
    'double': ('Float', 'Float', 8),
    'wgr_handle_t': ('WgrHandle', 'Int', 4),
    'wgr_color_t': ('WgrColor', 'Int', 4),
    'const char *': ('ConstCharStar', 'String', 4),
    'char *': ('ConstCharStar', 'String', 4),
    # the opaque user pointer: hxcpp passes it, js never needs to (the guest ABI
    # carries an id instead), so it has no JS mapping and those calls drop out there.
    'void *': ('VoidStar', None, 4),
}

# ------------------------------------------------------------- the headers ---


def read_headers():
    """What the binding is generated from: wgrender's public headers as clang reads them
    (tools/headers.py). Enums by name; each struct's fields as (type, name, array count);
    each function as (header, return type, name, params), params a list of (type, name),
    or None for what has no rendering here (a variadic call). In header order, and each
    header's declarations in the order it makes them."""
    api = headers.read(WGRENDER, tool='gen_raw')
    order = {h: i for i, h in enumerate(api.headers)}
    enums = {name for name in api.enums if name.startswith('wgr_') and name.endswith('_t')}
    structs = {name: [(f.type, f.name, f.count) for f in s.fields]
               for name, s in sorted(api.structs.items(), key=lambda kv: order[kv[1].header])}
    functions = [(f.header, f.returns, f.name, None if f.variadic else [(p.type, p.name) for p in f.params])
                 for f in sorted(api.functions.values(), key=lambda f: order[f.header])]
    return enums, structs, functions


# ------------------------------------------------------------- the output ---

DIGEST_TAG = '// wgrender-headers: '


def header_digest(wgrender):
    """A digest of wgrender's public headers: it moves when any of them does."""
    headers = sorted((wgrender / 'include').glob('*.h'))
    digest = hashlib.sha256()
    for h in headers:
        digest.update(h.name.encode())
        digest.update(h.read_bytes().replace(b'\r\n', b'\n'))  # the same on a CRLF checkout (Windows)
    return digest.hexdigest()[:16], len(headers)


def provenance():
    """What wgrender this was generated from, and a digest that moves when it does."""
    digest, count = header_digest(WGRENDER)
    values = headers.read(WGRENDER, tool='gen_raw').values  # the defines clang evaluated
    version = [str(values[f'WGR_VERSION_{part}']) for part in ('MAJOR', 'MINOR', 'PATCH')
               if f'WGR_VERSION_{part}' in values]
    try:
        commit = subprocess.run(['git', '-C', str(WGRENDER), 'describe', '--always', '--dirty'],
                                check=True, capture_output=True, text=True).stdout.strip()
    except Exception:
        commit = 'unknown'
    return '.'.join(version) or '?', commit, digest, count


def header_comment():
    version, commit, digest, count = provenance()
    return f"""package wgr.impl;

// GENERATED by tools/gen_raw_externs.py from wgrender's include/*.h — do not edit.
// Re-run the tool when wgrender's API moves; it writes this file whole.
//
// wgrender {version} at {commit}, {count} headers.
{DIGEST_TAG}{digest}
"""


def hx(ctype, enums, side):
    """The Haxe type for a C type, or None if there isn't one."""
    if ctype in SCALARS:
        return SCALARS[ctype][0 if side == 'cpp' else 1]
    if ctype in enums:
        return ('C' + ''.join(p.title() for p in ctype[4:-2].split('_'))) if side == 'cpp' else 'Int'
    if ctype in CALLBACKS:
        return ''.join(p.title() for p in ctype[4:-3].split('_')) + 'Fn' if side == 'cpp' else None
    if ctype in VALUES or ctype in OPAQUE:
        name = 'C' + ''.join(p.title() for p in ctype.replace('wgr_', '').removesuffix('_t').split('_'))
        return name if side == 'cpp' else VALUES.get(ctype)
    return None


def layout_name(ctype):
    """The JS-side offsets class for an opaque struct: wgr_keyboard_state_t -> KeyboardStateLayout."""
    return ''.join(p.title() for p in ctype.replace('wgr_', '').removesuffix('_t').split('_')) + 'Layout'


def camel(name):
    head, *rest = name.split('_')
    return head + ''.join(p.title() for p in rest)


def emit_layouts(structs, opaque_used):
    """Field offsets for the structs JS reads in place, so no one hand-counts them."""
    out = []
    for ctype in sorted(opaque_used):
        fields, size = layout(structs, ctype)
        lines = [f'/**',
                 f'\tWhere `{ctype}`\'s fields sit, in ints from the pointer. Offsets are',
                 f'\tgenerated from the header, so a layout change moves them rather than',
                 f'\tsilently misreading. An array field gives the index of its first element.',
                 f'**/',
                 f'class {layout_name(ctype)} {{',
                 f'\tpublic static inline var BYTES = {size};']
        for field, fctype, at, count in fields:
            assert at % 4 == 0 and SCALARS[fctype][2] == 4, f'{ctype}.{field} is not 4-byte'
            lines.append(f'\tpublic static inline var {camel(field)}'
                         f' = {at // 4};' + (f' // [{count}]' if count else ''))
        lines += ['',
                  '\tpublic static inline function read(pointer:Int, index:Int):Int',
                  '\t\treturn (Raw.host["HEAP32"] : Array<Int>)[(pointer >> 2) + index];',
                  '}']
        out.append('\n'.join(lines))
    return out


def emit_cpp(enums, structs, functions):
    used_enums, used_values, used_callbacks, body, skipped = set(), set(), set(), [], []
    for header, ret, name, params in functions:
        if name in SKIP:
            skipped.append((name, 'spec: skipped')); continue
        args = params
        if args is None:
            skipped.append((name, 'unparsed parameter list')); continue
        types = [ret] + [t for t, _ in args]
        bad = [t for t in types if hx(t, enums, 'cpp') is None]
        if bad:
            skipped.append((name, f'no mapping for {bad[0]}')); continue
        for t in types:
            (used_enums if t in enums else used_values if t in VALUES or t in OPAQUE
             else used_callbacks if t in CALLBACKS else set()).add(t)
        sig = ', '.join(f'{n}:{hx(t, enums, "cpp")}' for t, n in args)
        # C++ needs the enum type on the way in; on the way out it is a number, and an
        # opaque extern would be useless to the caller.
        hret = 'Int' if ret in enums else hx(ret, enums, 'cpp')
        body.append(f'\t@:native("{name}")\n\tstatic function {name}({sig}):{hret};')

    lines = [header_comment(), 'import cpp.ConstCharStar;', 'import cpp.RawPointer;', 'import cpp.UInt32;', '',
             'typedef WgrHandle = UInt32;', 'typedef WgrColor = UInt32;', '',
             '/** C `void *`: the opaque user pointer every wgrender callback carries. **/',
             'typedef VoidStar = RawPointer<cpp.Void>;',
             '/** The ABI types the callback plumbing shares with js; see Raw.js.hx. **/',
             'typedef CStr = ConstCharStar;', '']
    for c in sorted(used_callbacks):
        lines.append(f'typedef {hx(c, enums, "cpp")} = cpp.Callable<{CALLBACKS[c]}>;')
    lines.append('')
    lines.append('// Structs the API returns by value. C++ takes them as values; the JS side reads')
    lines.append('// them out of the heap (see Raw.js.hx).')
    for s in sorted(used_values):
        fields = '\n'.join(
            f'\t/** C `{t}[{c if c > 0 else "N"}]`, so it reads as a pointer here; index it in place. **/\n'
            f'\tvar {f}:cpp.RawPointer<{hx(t, enums, "cpp")}>;' if c else
            f'\tvar {f}:{hx(t, enums, "cpp") or ("C" + t)};'
            for f, t, c in ((f, t, c) for f, t, _, c in layout(structs, s)[0]))
        lines.append(f'@:include("wgr.h") @:native("{s}") @:structAccess @:unreflective\n'
                     f'extern class {hx(s, enums, "cpp")} {{\n{fields}\n}}\n')
    lines.append('// C enum parameter types: C++ will not take an `int` where the header names an')
    lines.append('// enum, so wgr\'s enum abstracts cast to these (see their `toRaw`).')
    for e in sorted(used_enums):
        lines.append(f'@:include("wgr.h") @:native("{e}") @:structAccess @:unreflective\n'
                     f'extern class {hx(e, enums, "cpp")} {{}}\n')
    lines.append(
        "// Where wgrender is and how to link it. This rides here because every program\n"
        "// that touches wgrender at all reaches this class, so -dce full cannot strip it\n"
        "// out from under the build the way it can any class in the API layer.\n"
        "//\n"
        "// project/Build.xml compiles wgrender's sources in, from the repository this\n"
        "// binding lives in, with whatever toolchain hxcpp chose; every build here goes\n"
        "// that way. -D WGR_BUILD_XML=<file> replaces it outright.\n"
        "@:buildXml('\n"
        '\t<include name="${WGR_BUILD_XML}" if="WGR_BUILD_XML" />\n'
        '\t<include name="${haxelib:wgrender-hx}/project/Build.xml" unless="WGR_BUILD_XML" />\n'
        "')\n"
        '@:keep @:unreflective @:include("wgr.h")\nextern class Raw {')
    lines.append('\n'.join(body))
    lines.append(MANUAL_CPP)
    lines.append('}')
    (OUT / 'Raw.cpp.hx').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return len(body), skipped


JS_PREAMBLE = '''
import wgr.Handle;
import wgr.MouseState;
import wgr.PickResult;
import wgr.Vec2;
import wgr.Vec3;

typedef WgrHandle = Int;
typedef WgrColor = Int;

/**
	The ABI types, so code that talks to wgrender's callbacks can be written once for
	both targets. On wasm every one of these is an Int -- an address, a table index, a
	32-bit float widened to Haxe's Float -- where hxcpp has a real pointer type for
	each. Naming them is what keeps `#if` out of the signatures that use them.
**/
typedef VoidStar = Int;
typedef CStr = Int;

/**
	The same C surface Raw.cpp.hx declares, reached through wgrender's JS binding
	(bindings/js/wgrender.js), which does the marshalling: strings on the wasm stack,
	released as each call returns; a struct through one fixed slot, read out at once
	into a plain object; the keyboard state's own slot. One marshalling for every JS
	guest, so Haxe's is the JS binding's and can't drift from it. Measured level with
	marshalling here in speed and GC (V8 removes the binding's intermediate objects), at
	about 3% more download for a trimmed page (2026-10-02).

	What stays here is the guest ABI's (`wgr.GuestAbi`), which the JS binding's own guest
	runtime doesn't share: the host module, a C string for the window title, the stack
	mark an op restores, and reads of the keyboard state in place.

	- **Names that cross into another module are quoted keys** (`WgrJs["wgr_..."]`,
	  `host["HEAP32"]`): a minifier that mangles properties leaves quoted ones alone,
	  and the other module's names can't change to match. V8 compiles a constant quoted
	  key exactly as a dotted one.
	- **Never hold a heap view.** The host links with `ALLOW_MEMORY_GROWTH`, so any
	  allocation detaches every `HEAP32` JS holds. Every read goes through
	  `host["HEAP32"]` at the point of use. Pointers survive growth; views do not.
**/
class Raw {
	/** The Emscripten module, handed over at boot. **/
	/**
		The host module's exports. Until `GuestAbi.attach` sets it, this is a stand-in
		that explains itself: a `static final` initialiser runs when the guest module
		loads, which is before any host exists, so `static final BG = Color.rgba(...)`
		fails there. Without this the browser says
		"Cannot read properties of undefined (reading '_wgr_color_rgba')", which names
		the call and not the cause. Build such values in the init op instead.
	**/
	/** Typed for string keys only (host["_wgr_..."]), so a dotted access, which a
		property-mangling minifier would break, doesn't compile. **/
	public static var host(default, null):haxe.DynamicAccess<Dynamic> = notAttached();

	static function notAttached():Dynamic {
		return new js.lib.Proxy<Dynamic>(cast {}, {
			get: (_, name, _) -> throw 'wgrender: $name was called before GuestAbi.attach(host). '
				+ 'A static initialiser runs when this module loads, before the host exists — '
				+ 'build wgrender values in the init op instead.'
		});
	}

	/**
		The array every vector getter fills (the JS binding's array form of `into`), read
		out at once by `Vec2.of` / `Vec3.of`: valid until the next vector getter.
	**/
	public static final vector:Array<Float> = [0.0, 0.0, 0.0];

	/** The host module, for the guest ABI; the boot page attaches the JS binding to it too. **/
	public static function attach(module:Dynamic):Void
		host = module;

	/** Where the op's stack started; `GuestAbi` restores to here when an op ends. **/
	public static inline function stackMark():Int
		return host["stackSave"]();

	public static inline function stackRelease(mark:Int):Void
		host["stackRestore"](mark);

	/**
		A Haxe string as a C string on the wasm stack, and `null` as a null pointer, for
		the guest ABI's calls (the window title) and `Native.cstr`'s shared signature. The
		caller releases it (`stackMark`, `stackRelease`).
	**/
	public static inline function cstr(s:String):Int {
		if (s == null)
			return 0;
		final length = host["lengthBytesUTF8"](s) + 1;
		final pointer = host["stackAlloc"](length);
		host["stringToUTF8"](s, pointer, length);
		return pointer;
	}
'''


def check():
    """Are the generated files current with the headers on disk?"""
    _, commit, digest, count = provenance()
    stale = []
    for name in ('Raw.cpp.hx', 'Raw.js.hx'):
        path = OUT / name
        if not path.exists():
            stale.append(f'{name}: missing')
            continue
        found = re.search(re.escape(DIGEST_TAG) + r'(\w+)', path.read_text(encoding='utf-8'))
        if not found:
            stale.append(f'{name}: no digest — generated before this was recorded')
        elif found.group(1) != digest:
            stale.append(f'{name}: generated from {found.group(1)}, headers are now {digest}')
    if stale:
        print(f'wgrender at {commit}: the binding is STALE')
        for s in stale:
            print(f'  {s}')
        print('  run tools/gen_raw_externs.py')
        return 1
    print(f'wgrender at {commit}, {count} headers: the binding is current ({digest})')
    return 0


def emit_built_version():
    """The wgrender this binding was generated against, for wgr.Version to compare."""
    version, commit, digest, _ = provenance()
    major, minor, patch = (version.split('.') + ['0', '0', '0'])[:3]
    (OUT / 'BuiltVersion.hx').write_text(header_comment() + f"""
/** What `wgr.Version` compares the running library against. **/
class BuiltVersion {{
	public static inline final MAJOR = {major};
	public static inline final MINOR = {minor};
	public static inline final PATCH = {patch};
	public static inline final STRING = "{version}";

	/** The wgrender commit these were generated from, when there was one. **/
	public static inline final COMMIT = "{commit}";

	/** A digest of the headers; `tools/gen_raw_externs.py --check` compares it. **/
	public static inline final HEADERS = "{digest}";
}}
""", encoding='utf-8')



# ------------------------------------------------- js: over the JS binding ---

def js_record(structs, ctype):
    """A Haxe anonymous structure for a record the JS binding returns as a plain object."""
    fields = []
    for field, ftype, _, count in layout(structs, ctype)[0]:
        if ftype in structs:
            t = js_record(structs, ftype)
        else:
            t = {'float': 'Float', 'bool': 'Bool'}.get(ftype, 'Int')
        fields.append(f'final {field}:{"Array<" + t + ">" if count else t}')
    return '{' + ' '.join(f + ';' for f in fields) + '}'


def from_record(structs, ctype, expr):
    """Haxe building the public layer's value from the JS binding's object `expr`, reading
    its fields by quoted key (`(r : haxe.DynamicAccess<Dynamic>)["x"]`): they cross from one file to another,
    so a property-mangling minifier must not rename them on this side only."""
    args = []
    for field, ftype, _, count in layout(structs, ctype)[0]:
        read = f'({expr} : haxe.DynamicAccess<Dynamic>)["{field}"]'
        if ftype in structs:
            args.append(from_record(structs, ftype, read))
        else:
            args.append(read)
    return f'new {VALUES.get(ctype, "Vec3")}({", ".join(args)})'


def js_call(name, params):
    """A call into the JS binding by quoted key, WgrJs["name"](...), so a minifier that
    mangles properties can't rename it on the Haxe side only. Looked up when called: the
    page sets WgrJs after this module has loaded."""
    holes = ', '.join('{' + str(i) + '}' for i in range(len(params)))
    args = ''.join(', ' + n for _, n in params)
    return f"js.Syntax.code('WgrJs[\"{name}\"]({holes})'{args})"


def emit_js(enums, structs, functions):
    """Raw.js.hx's calls as thin wrappers over the JS binding's functions, which do the
    marshalling (strings, records, the keyboard state's slot) themselves. Returns the
    wrappers, the extern declarations of the binding they call, what has no js rendering
    and why, and the opaque structs read in place."""
    externs, body, skipped, opaque_used = [], [], [], set()
    for header, ret, name, params in functions:
        if name in SKIP or params is None:
            continue
        types = [ret] + [t for t, _ in params]
        if any(t in CALLBACKS for t in types):
            skipped.append((name, 'takes a C callback — the guest ABI replaces it on js'))
            continue
        if ret not in OPAQUE and any(hx(t, enums, 'js') is None for t in types):
            continue
        if any(hx(t, enums, 'js') is None for t, _ in params):
            continue
        sig = ', '.join(f'{n}:{hx(t, enums, "js")}' for t, n in params)
        if ret in OPAQUE:
            opaque_used.add(ret)
            externs.append(f'\tstatic function {name}({sig}):Int;')
            body.append(f'\t/**\n'
                        f'\t\tA pointer to a `{ret}` in the wasm heap, too big to copy per frame: the\n'
                        f'\t\tpublic layer reads the fields it wants through `{layout_name(ret)}`. Valid\n'
                        f'\t\tuntil the next call to this getter.\n'
                        f'\t**/\n'
                        f'\tpublic static inline function {name}({sig}):Int\n\t\treturn {js_call(name, params)};')
        elif ret in VECTORS:
            # into the one reused array, by index: no object made, and nothing read by a
            # field name a property-mangling minifier could rename (Vec2.of / Vec3.of copy it out)
            externs.append(f'\tstatic function {name}({sig}{", " if sig else ""}into:Array<Float>):Array<Float>;')
            body.append(f'\tpublic static inline function {name}({sig}):Array<Float>\n'
                        f'\t\treturn {js_call(name, params + [("", "Raw.vector")])};')
        elif ret in VALUES:
            externs.append(f'\tstatic function {name}({sig}):{js_record(structs, ret)};')
            body.append(f'\tpublic static inline function {name}({sig}):{VALUES[ret]} {{\n'
                        f'\t\tfinal r:Dynamic = {js_call(name, params)};\n'
                        f'\t\treturn {from_record(structs, ret, "r")};\n\t}}')
        else:
            hret = 'String' if ret in ('const char *', 'char *') else hx(ret, enums, 'js')
            externs.append(f'\tstatic function {name}({sig}):{hret};')
            body.append(f'\tpublic static inline function {name}({sig}):{hret}\n'
                        f'\t\t{"" if hret == "Void" else "return "}{js_call(name, params)};')
    return body, externs, skipped, opaque_used


def main():
    if '--check' in sys.argv:
        sys.exit(check())
    enums, structs, functions = read_headers()
    print(f'{WGRENDER.name}: {len(functions)} functions, {len(enums)} enums, {len(structs)} structs')

    emit_built_version()
    n_cpp, cpp_skipped = emit_cpp(enums, structs, functions)
    js_body, js_externs, js_skipped, js_opaque = emit_js(enums, structs, functions)
    extern_class = ('/** The JS binding (bindings/js/wgrender.js), which the boot page puts on the global\n'
                    '    as WgrJs before the guest starts. **/\n'
                    '@:native("WgrJs") extern class WgrJs {\n' + '\n'.join(js_externs) + '\n}\n')
    (OUT / 'Raw.js.hx').write_text(header_comment() + JS_PREAMBLE + '\n' + '\n\n'.join(js_body)
                                   + '\n' + MANUAL_JS + '\n}\n\n' + extern_class
                                   + ''.join('\n' + c + '\n' for c in emit_layouts(structs, js_opaque)),
                                   encoding='utf-8')

    print(f'  Raw.cpp.hx  {n_cpp} externs')
    print(f'  Raw.js.hx   {len(js_body)} wrappers over the JS binding')
    by_reason = {}
    for name, why in cpp_skipped:
        by_reason.setdefault(why, []).append(name)
    if by_reason:
        print('\nnot bound (hxcpp):')
        for why, names in sorted(by_reason.items(), key=lambda kv: -len(kv[1])):
            print(f'  {len(names):>3}  {why}')
            print(f'       {", ".join(sorted(names)[:4])}{" …" if len(names) > 4 else ""}')
    if js_skipped:
        print(f'\nhxcpp only ({len(js_skipped)}): the guest ABI replaces these on js')
        print(f'  {", ".join(sorted(n for n, _ in js_skipped))}')


if __name__ == '__main__':
    main()
