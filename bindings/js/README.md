# wgrender's JS binding

wgrender from JavaScript and TypeScript in the browser: plain ES modules with TypeScript
declarations beside them, generated from wgrender's public headers. The game runs as a
JS *guest* of wgrender's wasm *host* (the guest ABI, `bindings/host/wgr_guest.h`), the
same arrangement the Haxe binding's JS target uses.

An experiment for now (2026-10-02): proving that a generated binding is usable before
libwgt's binding phase builds on the same approach.

## Files

| | |
|---|---|
| `wgrender.js`, `wgrender.d.ts` | every C function under its C name, the enums, the integer defines (colors, window flags) and the records; generated, never edited |
| `wgrender.exports.json` | the wasm exports the binding calls, for the host's link; generated |
| `src/runtime.js` | how a call crosses: strings in, records out, the scratch arena |
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

A call that passes a string or returns a record uses the op's scratch arena, released
when the op ends; outside an op, wrap such calls in `arena()` from `src/runtime.js`.

## Measured

The stress scene at 5,000 entities, against the Haxe binding's JS guest on the same scene
(Chromium on Linux, Xvfb, WebGL2, wasm32-release, Emscripten 5.0.7, 2026-10-02):

| | script ms per frame (3 runs) | JS allocation | GC (10 s, 8 ms load a frame) |
|---|---|---|---|
| JS binding | 1.71, 1.79, 1.56 | 72.8 MB/min | 5-6 minor, max 0.8 ms, no late frames |
| Haxe JS guest | 1.63, 1.57, 1.79 | 78.8 MB/min | 5-6 minor, max 0.7 ms, no late frames |

Level: the same marshalling, so the same cost. The host here exports every call (528),
which is what a hot-reloading guest needs; trimming it to what one program imports comes
later, from a bundler's module graph.

## Build and check

```
source <emsdk>/emsdk_env.sh
python3 bindings/js/tools/gen_binding.py      # after a header changes
python3 bindings/js/tools/check_binding.py    # TSC=<path to tsc> for the TypeScript step
python3 tools/serve_site.py 8000 out/wasm32/release/site/js
# http://localhost:8000/examples/hello/
```
