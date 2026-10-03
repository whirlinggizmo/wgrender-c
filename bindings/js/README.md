# wgrender's JS binding

wgrender from JavaScript and TypeScript in the browser: plain ES modules with TypeScript
declarations beside them, generated from wgrender's public headers. The game runs as a
JS *guest* of wgrender's wasm *host* (the guest ABI, `bindings/host/wgr_guest.h`).
The Haxe binding's JS target calls through it (since 2026-10-03), so it is the one
marshalling for every JS guest; `wgrender.exports.json` is what a host links for it,
read by `tools/build_host.py` and by Haxe's `WebHost`.

## Files

| | |
|---|---|
| `wgrender.js`, `wgrender.d.ts` | every C function under its C name, the enums, the integer defines (colors, window flags) and the records; generated, never edited |
| `wgrender.exports.json` | the wasm exports the binding calls, for the host's link; generated |
| `src/runtime.js` | how a call crosses: strings in on the stack, records out through one fixed slot |
| `src/guest.js` | the ops (`register`, `registerTick`, `start`) and their fault handling |
| `examples/` | `hello` and `stress` (the scene every binding ports, `tools/bench/stress.c`) |
| `tests/types.ts` | what TypeScript must accept and reject |
| `tools/gen_binding.py` | writes the generated files from the headers (clang); `--check` says whether they're current |
| `tools/build_host.py` | the host (wgrender's web library plus the guest glue) and the site, in `out/wasm32/release/site/js/` |
| `tools/check_binding.py` | all of it: current, typed, built, and every example run in a browser |

The headers are the documentation: each call keeps its C name, and its header comment is
its JSDoc, so an editor shows the same text the header has.

## Use

```js
import createWgrHost from './wgrender-host.js';
import * as wgr from './wgrender.js';
import * as guest from './src/guest.js';

function frame(dt) {
    wgr.wgr_render_begin_frame();
    wgr.wgr_render_clear_background(wgr.WGR_COLOR_RAYWHITE);
    wgr.wgr_text_draw('hello', 40, 40, 32, wgr.WGR_COLOR_DARKGRAY);
    wgr.wgr_render_end_frame();
}

const host = await createWgrHost({ canvas: document.getElementById('canvas') });
guest.register({ frame });
guest.start(host, 800, 600, 'hello');
```

Nothing a call allocates outlives it: a string goes in on the wasm stack and is released
as the call returns, and a record comes back through one fixed slot, read out at once.
(An arena that lasted the whole op overflowed the 64 KB wasm stack at 5,000 vec3 getters
in one frame.) The keyboard state's pointer is valid until the next call that reads it.

## Measured

The stress scene at 5,000 entities, against the Haxe binding's JS guest on the same scene
(Chromium on Linux, Xvfb, WebGL2, wasm32-release, Emscripten 5.0.7, 2026-10-02):

| | script ms per frame (3 runs) | JS allocation | GC (10 s, 8 ms load a frame) |
|---|---|---|---|
| JS binding | 1.71, 1.79, 1.56 | 72.8 MB/min | 5-6 minor, max 0.8 ms, no late frames |
| Haxe JS guest | 1.63, 1.57, 1.79 | 78.8 MB/min | 5-6 minor, max 0.7 ms, no late frames |

Level: the same marshalling, so the same cost. (The Haxe guest marshalled for itself
then, the same way; it has called through this binding since.) The host here exports every call (528),
which is what a hot-reloading guest needs; trimming it to what one program imports comes
later, from a bundler's module graph.

## Minifying

Plain minification (esbuild `--minify`, terser's defaults: local names and whitespace)
is safe. Mangling **property** names (esbuild `--mangle-props`, terser
`mangle.properties`, Closure's advanced mode) is safe for the binding, with two rules
for your own code:

- **Keep `wgr_` out of the pattern.** The binding's functions are module exports;
  mangling export names breaks a namespace import (`import * as wgr`) in a bundle, as it
  would for any ES module.
- **Read vectors into an array if your own `x`/`y`/`z` get mangled.**
  `wgr_sprite3d_get_position(s, v)` fills `v` by index when it is an array or a typed
  array, which nothing can rename. With an object of your own, or the object a getter
  returns, your mangled `x` is not the binding's `x`: it runs without an error and
  reads `0` or `undefined`.

Everything the binding itself sends across a file boundary is a quoted key
(`host["_wgr_..."]`, `into["x"]`), which manglers leave alone; V8 compiles a constant
quoted key exactly as a dotted one, so it costs nothing.

Measured (2026-10-03, esbuild 0.28, each file minified separately with
`--mangle-props=^(_|HEAP|stack|wgr_|x$|y$|z$)`, 5,000 position reads a frame, the sum
checked on screen): a getter into an array reads right, separately minified or bundled
(bundled with `wgr_` left out of the pattern); a returned object read `NaN`, and `into` an
object read `0`.

## Build and check

```
source <emsdk>/emsdk_env.sh
python3 bindings/js/tools/gen_binding.py      # after a header changes
python3 bindings/js/tools/check_binding.py    # TSC=<path to tsc> for the TypeScript step
python3 tools/serve_site.py 8000 out/wasm32/release/site/js
# http://localhost:8000/examples/hello/
```

`build_host.py` makes a **full** host by default: it exports every function, so any
program runs against it, which is what development and a hot-reloading guest need.
`--trimmed LISTING` makes a release host instead, exporting only the functions LISTING
names, with `wgrender.js` trimmed to the same: the stress scene's wasm is 158 KB gzipped
trimmed against 331 KB full, and its `wgrender.js` 3.9 KB against 34 KB. The listing is
what the program calls; for plain JS it has to come from something that parses the
program (a bundler that reports the exports it kept), and nothing here derives it yet.
The Haxe binding derives its own (`-D wgr-host=trimmed`, its default).
