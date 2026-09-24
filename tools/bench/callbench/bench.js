// callbench: what a call from JS into wgrender's wasm costs, against the same call
// made inside the wasm. index.html runs it in the browser; measure.py builds the wasm
// (shapes.c, loops.c), serves the page and reads the result.
//
// The JS side marshals the way a JS guest binding does (wgrender-hx's Raw.js.hx):
// a returned bool compared with 0, struct results read out of the heap into a new
// value object, strings copied in with stringToUTF8, and scratch memory taken from the
// wasm stack with stackAlloc and given back once per batch of calls, as a guest does
// once per op. The result: ns per call, the median of `reps` runs.

const BATCH = 256; // calls between stack restores; 256 x 48 bytes fits the default stack

export function measure(m, N, REPS) {
    const h = m._cb_make_handle();

    class MouseState {
        constructor(x, y, dx, dy, wheel, down, pressed, buttons, released, over) {
            this.x = x; this.y = y; this.dx = dx; this.dy = dy; this.wheel = wheel; this.down = down;
            this.pressed = pressed; this.buttons = buttons; this.released = released; this.over = over;
        }
    }
    const cstr = (s) => {
        const length = m.lengthBytesUTF8(s) + 1;
        const pointer = m.stackAlloc(length);
        m.stringToUTF8(s, pointer, length);
        return pointer;
    };
    const batched = (call) => () => {
        let s = 0;
        for (let k = 0; k < N; k += BATCH) {
            const mark = m.stackSave();
            for (let j = 0; j < BATCH; j++) s += call(k + j);
            m.stackRestore(mark);
        }
        return s;
    };

    const shapes = {
        tint: {
            c: "wgr_model_set_tint",
            js: batched((k) => (m._cb_set_tint(h, k) != 0 ? 1 : 0)),
            wasm: () => m._cb_loop_tint(N, h),
        },
        transform: {
            c: "wgr_model_set_transform",
            js: batched((k) => (m._cb_set_transform(h, k, 1, 2, 3, 4, 5, 6, 7, 8) != 0 ? 1 : 0)),
            wasm: () => m._cb_loop_transform(N, h),
        },
        struct: {
            c: "wgr_input_get_mouse_state",
            js: batched(() => {
                const out = m.stackAlloc(48);
                m._cb_get_mouse(out);
                const i32 = m.HEAP32, b = out >> 2;
                return new MouseState(i32[b], i32[b + 1], i32[b + 2], i32[b + 3], i32[b + 4], i32[b + 5],
                                      i32[b + 6], [i32[b + 7], i32[b + 8], i32[b + 9]], i32[b + 10], i32[b + 11]).dy;
            }),
            wasm: () => m._cb_loop_mouse(N),
        },
        string: {
            c: "wgr_text_measure",
            js: batched(() => m._cb_measure(cstr("Hello, wgrender"), 20)),
            wasm: () => m._cb_loop_measure(N),
        },
    };

    const time = (f) => {
        const ns = [];
        for (let r = 0; r < REPS; r++) {
            const t = performance.now();
            f();
            ns.push((performance.now() - t) * 1e6 / N);
        }
        ns.sort((a, b) => a - b);
        return ns[REPS >> 1];
    };
    const out = {};
    for (const [name, s] of Object.entries(shapes)) {
        time(s.js); time(s.wasm); // warm both up first
        const js = time(s.js), wasm = time(s.wasm);
        out[name] = { c: s.c, jsNs: +js.toFixed(2), wasmNs: +wasm.toFixed(2), boundaryNs: +(js - wasm).toFixed(2) };
    }
    return { calls: N, reps: REPS, shapes: out };
}
