// Types for src/runtime.js: how a call crosses into wgrender's wasm host.

/** The Emscripten module, once attach() has run. */
export declare let host: any;
/** Hand the binding its host: the module createWgrHost() resolved to. */
export declare function attach(module: unknown): void;
/** A NUL-terminated copy of `s` on the wasm stack; null for a null pointer. */
export declare function cstr(s: string | null | undefined): number;
/** A C string out ("" for a null pointer). */
export declare function str(pointer: number): string;
/** The one slot a record comes back through (at least `bytes`); read it before the next. */
export declare function record(bytes: number): number;
/** An opaque record's own slot: valid until the next call to the same getter. */
export declare function opaqueSlot(name: string, bytes: number): number;
/** A 32-bit word at `index` words from `pointer`, for the opaque layouts. */
export declare function readI32(pointer: number, index: number): number;
