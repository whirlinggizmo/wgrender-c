// The JS binding's runtime: how a call crosses into wgrender's wasm host and back.
// Hand-written; wgrender.js (generated) is built on it.
//
// Three rules, all measured hazards (the Haxe binding's Raw.js.hx keeps the same):
//
// - Never hold a heap view. The host links with ALLOW_MEMORY_GROWTH, so any allocation
//   can replace host.HEAPF32 and the rest. Every read goes through `host.` at the point
//   of use; a pointer survives growth, a view doesn't.
// - Nothing accumulates within an op. A record comes back through one fixed slot
//   (record()), malloc'd once and read out at once, so every getter reuses it. A string
//   goes in on the wasm stack, and the call that passed it releases it as it returns
//   (the generated code saves and restores the stack around the call). An arena that
//   lasted the whole op overflowed the 64 KB wasm stack: 5,000 vec3 getters in one
//   frame need 80 KB (2026-10-02, measured in the stress scene).
// - Records come back through a pointer. wasm returns anything bigger than a scalar
//   through a hidden first argument, so the generated getters read the fields out of
//   the heap into a fresh object. An opaque record (the keyboard state) has a slot of
//   its own, valid until the next call to the same getter, since it is read lazily.

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
    slot = 0;
    slotBytes = 0;
    opaque.clear();
}

let slot = 0;
let slotBytes = 0;
const opaque = new Map();

/** The one slot a record comes back through, at least `bytes` long. Read it before the
 * next call that returns a record. */
export function record(bytes) {
    if (bytes > slotBytes) {
        if (slot) host._free(slot);
        slotBytes = Math.max(bytes, 256);
        slot = host._malloc(slotBytes);
    }
    return slot;
}

/** An opaque record's own slot, `bytes` long: valid until the next call to `name`. */
export function opaqueSlot(name, bytes) {
    let pointer = opaque.get(name);
    if (pointer === undefined) {
        pointer = host._malloc(bytes);
        opaque.set(name, pointer);
    }
    return pointer;
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


/** A 32-bit word at `index` words from `pointer`, for the opaque layouts (WGR_KEYBOARD_STATE). */
export function readI32(pointer, index) {
    return host.HEAP32[(pointer >> 2) + index];
}
