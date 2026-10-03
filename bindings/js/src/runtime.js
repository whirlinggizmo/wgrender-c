// The JS binding's runtime: how a call crosses into wgrender's wasm host and back.
// Hand-written; wgrender.js (generated) is built on it.
//
// Three rules, all measured hazards (the Haxe binding's Raw.js.hx keeps the same):
//
// - Never hold a heap view. The host links with ALLOW_MEMORY_GROWTH, so any allocation
//   can replace host.HEAPF32 and the rest. Every read goes through `host.` at the point
//   of use; a pointer survives growth, a view doesn't.
// - Scratch is an arena per op. Strings in and record slots out come from the wasm
//   stack (stackAlloc), and guest.js restores the stack once when each op ends, fault or
//   not. So a call that passes a string or returns a record belongs inside an op (init,
//   frame, tick, shutdown); outside one, wrap it in arena().
// - Records come back through a pointer. wasm returns anything bigger than a scalar
//   through a hidden first argument, so the generated getters read the fields out of
//   the heap into a fresh object.

/** The Emscripten module, once attach() has run. Until then, a stand-in that says why. */
export let host = new Proxy({}, {
    get(_, name) {
        throw new Error(`wgrender: ${String(name)} was reached before attach(host). A call at module `
            + `load runs before the host exists; make wgrender values in the init op instead.`);
    },
});

/** Hand the binding its host: the module createWgrHost() resolved to. */
export function attach(module) {
    host = module;
}

/** A NUL-terminated copy of `s` on the wasm stack, and null as a null pointer (a C API may
 * tell NULL from "", as wgr_asset_ensure does). */
export function cstr(s) {
    if (s === null || s === undefined) return 0;
    const length = host.lengthBytesUTF8(s) + 1;
    const pointer = host.stackAlloc(length);
    host.stringToUTF8(s, pointer, length);
    return pointer;
}

/** A C string out, as a JS string ("" for a null pointer). */
export function str(pointer) {
    return pointer === 0 ? '' : host.UTF8ToString(pointer);
}

/** `bytes` of the op's arena: a slot for a record to be written into. */
export function scratch(bytes) {
    return host.stackAlloc(bytes);
}

/** A 32-bit word at `index` words from `pointer`, for the opaque layouts (WGR_KEYBOARD_STATE). */
export function readI32(pointer, index) {
    return host.HEAP32[(pointer >> 2) + index];
}

/** Run `fn` with its own scratch arena, released when it returns or throws: for calls
 * made outside an op. */
export function arena(fn) {
    const mark = host.stackSave();
    try {
        return fn();
    } finally {
        host.stackRestore(mark);
    }
}
