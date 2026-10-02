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
  opaque      a struct too big to copy per frame; JS gets a heap pointer and a
              generated table of field offsets to read it with
  skips       varargs, and anything else with no sane rendering

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
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--check',), positional=0)

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'src/wgr/impl'

# ---------------------------------------------------------------- the spec ---

# C callback typedefs: the Haxe function type, and whether the JS layer can offer it.
# The guest ABI (host/wgr_guest.h) replaces every one of these on js, so none can.
CALLBACKS = {
    'wgr_lifecycle_fn': '(user:VoidStar) -> Void',
    'wgr_frame_fn': '(dt:Single, tickFraction:Single, user:VoidStar) -> Void',
    'wgr_tick_fn': '(dt:Single, user:VoidStar) -> Void',
    'wgr_asset_callback_fn': '(path:ConstCharStar, user:VoidStar) -> Void',
    'wgr_event_callback_fn': '(event:VoidStar, user:VoidStar) -> Void',
    'wgr_event_listener_fn': '(payload:VoidStar, user:VoidStar) -> Void',
    'wgr_asset_fetch_fn': '(request:WgrHandle, url:ConstCharStar, destPath:ConstCharStar, user:VoidStar) -> Void',
    'wgr_asset_ping_fn': '(host:ConstCharStar, milliseconds:Single, user:VoidStar) -> Void',
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

# No sane rendering: varargs, or a pointer the public API rule says should not exist.
SKIP = {
    'wgr_logger_message', 'wgr_logger_message_source',  # varargs — re-added in MANUAL
}

# Declarations the parser can't produce, rendered verbatim. Keep this short: anything
# here is a thing the generator does not know, and every entry is a thing to re-check
# when wgrender's API moves.
MANUAL_CPP = '''
	/** The varargs logger, fixed at one `%s` — enough for a Haxe string. **/
	@:native("wgr_logger_message")
	static function wgr_logger_message(level:CLogLevel, format:ConstCharStar, text:ConstCharStar):Void;

	/** The same, with the call site the WGR_LOG_* macros would have filled in. **/
	@:native("wgr_logger_message_source")
	static function wgr_logger_message_source(level:CLogLevel, sourceFile:ConstCharStar, sourceLine:Int,
		format:ConstCharStar, text:ConstCharStar):Void;'''

MANUAL_JS = '''
	/**
		One `%s` argument as a C `va_list`, in the op's arena.

		A variadic C function does not reach wasm as one: clang lowers it to take a
		pointer to the argument area as its last parameter. So passing the string
		pointer straight through hands the callee a va_list whose first four bytes are
		the text -- `wgr_logger_message(Info, "%s", "hello")` made the C read 0x6c6c6568
		as a pointer and trapped with "memory access out of bounds". It has to be a
		real slot holding the pointer, and the address of the slot is what goes across.

		8 bytes because that is the varargs area's alignment; only the first 4 are used.
	**/
	static inline function vaString(text:String):Int {
		final value = cstr(text);
		final area = scratch(8);
		host.HEAP32[area >> 2] = value;
		return area;
	}

	/**
		The callback-taking calls, which the generator skips because a `void *` has no
		js mapping. On wasm both a function pointer and a `void *` are an `Int` -- a
		table index and an address -- so these are mechanical once someone says so.

		They are here rather than in the generator's output because saying "`void *` is
		an Int on js" in general would also un-skip wgr_set_init and friends, which are
		omitted on purpose: the guest ABI is their replacement, and a guest reaching
		past it would install a second frame callback.
	**/
	public static inline function wgr_event_on(event_name:String, listener:Int, user_data:Int):Int
		return Raw.host._wgr_event_on(cstr(event_name), listener, user_data);

	public static inline function wgr_event_once(event_name:String, listener:Int, user_data:Int):Int
		return Raw.host._wgr_event_once(cstr(event_name), listener, user_data);

	public static inline function wgr_event_off(event_name:String, listener:Int, user_data:Int):Int
		return Raw.host._wgr_event_off(cstr(event_name), listener, user_data);

	public static inline function wgr_event_emit(event_name:String, payload:Int):Int
		return Raw.host._wgr_event_emit(cstr(event_name), payload);

	public static inline function wgr_asset_ping_host(host_:String, timeout_ms:Int, on_done:Int,
			user_data:Int):Bool
		return Raw.host._wgr_asset_ping_host(cstr(host_), timeout_ms, on_done, user_data) != 0;

	/** Fill wgrender's lifecycle slots with the guest glue's dispatchers. **/
	public static inline function wgr_guest_install():Void
		Raw.host._wgr_guest_install();

	/** Turn a Haxe function into a wasm table index; `removeFunction` gives it back. **/
	public static inline function addFunction(fn:Dynamic, signature:String):Int
		return Raw.host.addFunction(fn, signature);

	public static inline function removeFunction(pointer:Int):Void
		Raw.host.removeFunction(pointer);

	/** The varargs logger, fixed at one `%s` — enough for a Haxe string. **/
	public static inline function wgr_logger_message(level:Int, format:String, text:String):Void
		Raw.host._wgr_logger_message(level, cstr(format), vaString(text));

	/** The same, with the call site the WGR_LOG_* macros would have filled in. **/
	public static inline function wgr_logger_message_source(level:Int, sourceFile:String, sourceLine:Int,
			format:String, text:String):Void
		Raw.host._wgr_logger_message_source(level, cstr(sourceFile), sourceLine, cstr(format), vaString(text));'''

SCALARS = {  # C type -> (hxcpp, js), size, how JS reads it out of the heap
    'void': ('Void', 'Void', 0, None),
    'bool': ('Bool', 'Bool', 4, 'HEAPU8:1'),
    'int': ('Int', 'Int', 4, 'HEAP32:4'),
    'unsigned int': ('UInt32', 'Int', 4, 'HEAPU32:4'),
    'unsigned': ('UInt32', 'Int', 4, 'HEAPU32:4'),
    'float': ('Single', 'Float', 4, 'HEAPF32:4'),
    'double': ('Float', 'Float', 8, None),
    'wgr_handle_t': ('WgrHandle', 'Int', 4, 'HEAPU32:4'),
    'wgr_color_t': ('WgrColor', 'Int', 4, 'HEAPU32:4'),
    'const char *': ('ConstCharStar', 'String', 4, None),
    'char *': ('ConstCharStar', 'String', 4, None),
    # the opaque user pointer: hxcpp passes it, js never needs to (the guest ABI
    # carries an id instead), so it has no JS mapping and those calls drop out there.
    'void *': ('VoidStar', None, 4, None),
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


def layout(structs, name):
    """Field offsets in bytes, C rules — every member here is 4-aligned or a nest."""
    offsets, at = [], 0
    for ctype, field, count in structs[name]:
        if ctype in structs:
            size = layout(structs, ctype)[1] * max(count, 1)
            offsets.append((field, ctype, at, count))
        else:
            unit = SCALARS.get(ctype, (None, None, 4, None))[2]
            size = unit * max(count, 1)
            offsets.append((field, ctype, at, count))
        at += size
    return offsets, at


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
    defines = headers.read(WGRENDER, tool='gen_raw').defines
    version = [defines[f'WGR_VERSION_{part}'][0] for part in ('MAJOR', 'MINOR', 'PATCH')
               if f'WGR_VERSION_{part}' in defines]
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
                  '\t\treturn (Raw.host.HEAP32 : Array<Int>)[(pointer >> 2) + index];',
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
             'typedef CStr = ConstCharStar;', 'typedef F32 = Single;', '']
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


def emit_js(enums, structs, functions):
    body, skipped, values_used, opaque_used = [], [], set(), set()
    for header, ret, name, params in functions:
        if name in SKIP:
            continue
        args = params
        if args is None:
            continue
        types = [ret] + [t for t, _ in args]
        if any(t in CALLBACKS for t in types):
            skipped.append((name, 'takes a C callback — the guest ABI replaces it on js'))
            continue
        if ret in OPAQUE:
            if any(hx(t, enums, 'js') is None for t, _ in args):
                continue
            opaque_used.add(ret)
            osig = ', '.join(f'{n}:{hx(t, enums, "js")}' for t, n in args)
            ocall = ', '.join(f'cstr({n})' if t in ('const char *', 'char *') else n
                              for t, n in args)
            size = layout(structs, ret)[1]
            body.append(
                f'\t/**\n'
                f'\t\tA pointer to a `{ret}` in the wasm heap — {size} bytes is too much to\n'
                f'\t\tcopy per frame, so the public layer reads the fields it wants through\n'
                f'\t\t`{layout_name(ret)}`. Valid until the current guest op returns, which is\n'
                f'\t\twhen the op edge resets the stack this was taken from.\n'
                f'\t**/\n'
                f'\tpublic static function {name}({osig}):Int {{\n'
                f'\t\tfinal out = scratch({size});\n'
                f'\t\tRaw.host._{name}(out{", " if ocall else ""}{ocall});\n'
                f'\t\treturn out;\n\t}}')
            continue
        if any(hx(t, enums, 'js') is None for t in types):
            continue
        sig = ', '.join(f'{n}:{hx(t, enums, "js")}' for t, n in args)
        call_args = ', '.join(f'cstr({n})' if t in ('const char *', 'char *') else n for t, n in args)
        if ret in VALUES:
            values_used.add(ret)
            fields, size = layout(structs, ret)
            reads, heaps = [], set()
            for field, ctype, at, count in fields:
                if ctype in structs and ctype not in SCALARS:
                    sub, _ = layout(structs, ctype)
                    heaps.add('HEAPF32')
                    reads.append(f'new {VALUES.get(ctype, "Vec3")}('
                                 + ', '.join(f'f32[base + {(at + o) // 4}]' for _, _, o, _ in sub) + ')')
                elif count:
                    heaps.add('HEAP32')
                    reads.append('[' + ', '.join(f'i32[base + {(at + i * 4) // 4}]' for i in range(count)) + ']')
                else:
                    heap, width = SCALARS[ctype][3].split(':')
                    heaps.add(heap)
                    if width == '1':
                        reads.append(f'u8[out + {at}] != 0')
                    else:
                        reads.append(f'{ {"HEAP32": "i32", "HEAPU32": "u32", "HEAPF32": "f32"}[heap] }[base + {at // 4}]')
            locals_ = '\n\t\t'.join(
                f'final {v} = Raw.host.{k};' for k, v in
                [('HEAP32', 'i32'), ('HEAPU32', 'u32'), ('HEAPF32', 'f32'), ('HEAPU8', 'u8')] if k in heaps)
            body.append(
                f'\tpublic static function {name}({sig}):{VALUES[ret]} {{\n'
                f'\t\tfinal out = scratch({size});\n'
                f'\t\tRaw.host._{name}(out{", " if call_args else ""}{call_args});\n'
                f'\t\t{locals_}\n\t\tfinal base = out >> 2;\n'
                f'\t\treturn new {VALUES[ret]}({", ".join(reads)});\n\t}}')
        else:
            call = f'Raw.host._{name}({call_args})'
            hret = hx(ret, enums, 'js')
            line = (f'\t\t{call};' if hret == 'Void'
                    else f'\t\treturn {call} != 0;' if hret == 'Bool'
                    else f'\t\treturn str({call});' if ret in ('const char *', 'char *')
                    else f'\t\treturn {call};')
            body.append(f'\tpublic static inline function {name}({sig}):'
                        f'{"String" if ret in ("const char *", "char *") else hret}\n{line}')
    return body, skipped, values_used, opaque_used


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
typedef F32 = Float;
typedef EventListenerFn = Int;
typedef AssetPingFn = Int;

/**
	The same C surface Raw.cpp.hx declares, reached through the host module's exports.

	Three rules this layer keeps, all of them measured hazards:

	- **Never hold a heap view.** The host links with `ALLOW_MEMORY_GROWTH`, so any
	  allocation detaches every `HEAPF32`/`HEAP32` JS holds. Every read goes through
	  `host.HEAPF32` at the point of use. Pointers survive growth; views do not.
	- **Scratch is an arena per op.** Strings and struct-return slots come from
	  `stackAlloc`, and the guest's op edge restores the stack pointer once when the op
	  ends, fault or not (`wgr.GuestAbi`).
	- **Structs come back through a pointer.** The wasm C ABI returns anything larger
	  than a scalar through a hidden first argument, so these read the fields out of
	  the heap rather than getting a value.
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
	public static var host(default, null):Dynamic = notAttached();

	static function notAttached():Dynamic {
		return new js.lib.Proxy<Dynamic>(cast {}, {
			get: (_, name, _) -> throw 'wgrender: $name was called before GuestAbi.attach(host). '
				+ 'A static initialiser runs when this module loads, before the host exists — '
				+ 'build wgrender values in the init op instead.'
		});
	}

	public static function attach(module:Dynamic):Void {
		host = module;
	}

	/** Where the op arena started; `GuestAbi` restores to here when an op ends. **/
	public static inline function stackMark():Int
		return host.stackSave();

	public static inline function stackRelease(mark:Int):Void
		host.stackRestore(mark);

	/** A NUL-terminated copy of `s` in the op's arena. **/
	/**
		A Haxe string as a C string in the wasm heap, and `null` as a null pointer.

		The distinction matters: wgr_asset_ensure_async treats a null fetch_url as "use
		the host, with redirects and variants" and a non-null one as "the caller chose
		this exact file". An empty string is not the same thing, so null has to survive
		the crossing — as it already does on hxcpp, through Native.cstr.
	**/
	public static inline function cstr(s:String):Int {
		if (s == null)
			return 0;
		final length = host.lengthBytesUTF8(s) + 1;
		final pointer = host.stackAlloc(length);
		host.stringToUTF8(s, pointer, length);
		return pointer;
	}

	static inline function scratch(bytes:Int):Int
		return host.stackAlloc(bytes);

	public static inline function str(pointer:Int):String
		return host.UTF8ToString(pointer);

	/** The guest ABI's asset op, keyed by `id` — the guest never sees a callback. **/
	public static inline function wgr_guest_asset_load(path:String, id:Int, fetchUrl:String, flags:Int):Bool
		return host._wgr_guest_asset_load(cstr(path), id, cstr(fetchUrl), flags) != 0;
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


def main():
    if '--check' in sys.argv:
        sys.exit(check())
    enums, structs, functions = read_headers()
    print(f'{WGRENDER.name}: {len(functions)} functions, {len(enums)} enums, {len(structs)} structs')

    emit_built_version()
    n_cpp, cpp_skipped = emit_cpp(enums, structs, functions)
    js_body, js_skipped, _, js_opaque = emit_js(enums, structs, functions)
    layouts = emit_layouts(structs, js_opaque)
    (OUT / 'Raw.js.hx').write_text(header_comment() + JS_PREAMBLE + '\n' + '\n\n'.join(js_body)
                                   + '\n' + MANUAL_JS + '\n}\n'
                                   + ''.join('\n' + c + '\n' for c in layouts), encoding='utf-8')

    print(f'  Raw.cpp.hx  {n_cpp} externs')
    print(f'  Raw.js.hx   {len(js_body)} wrappers')
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
