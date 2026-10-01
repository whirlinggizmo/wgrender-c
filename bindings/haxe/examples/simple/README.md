# simple — wgrender as a host, the game as a guest

A port of wgrender's `simple` that runs the same scene on web and desktop from one
source, next to [../simple-hxcpp](../simple-hxcpp) (Haxe → hxcpp → wasm, all in one
module). The binding is the [wgrender-hx](../../github/whirlinggizmo/wgrender-hx)
haxelib, shared between them. Here the split is the other way round:
**libwgrender is a wasm host and the game is a guest module in JS**, compiled from
Haxe.

The point is to find out what that costs and what it feels like, against the same
scene. `../simple-hxcpp` is the all-in-one comparison against the Nim and Beef ports; this
is the "ship it" variant.

## The shape

It follows [wg-vf](../../github/whirlinggizmo/wg-vf)'s host/guest split, applied to a
renderer rather than a simulation:

| The guest owns | The host owns |
|---|---|
| Game state and logic | When ops are called, and in what order |
| The handles it creates | The window, the loop, assets, input, the scene |

`host/wgr_guest.h` is the whole contract — four ops (`init`, `frame`, `asset`,
`shutdown`), registered once. wgrender was already most of the way to being a host:
it owns the loop through `sapp_run`, hands out handles rather than pointers, and
keeps a retained scene that draws in one call.

Guarantees the host makes, same as wg-vf's:

1. **Serial, non-reentrant, single-threaded.** Ops are never called concurrently or
   nested. The guest *may* call wgrender's API during an op — that is the point — but
   the host will not re-enter the guest while one is running.
2. **`init` first, `shutdown` last.**
3. **`asset` fires on the main thread during a later frame**, never inside another op.

Ops return 0 for ok, nonzero for a fault. A guest in a language with exceptions
catches at its own edge and converts a throw into a nonzero return — an exception that
escapes into the host's C frames takes the loop down (measured: on the web the page
freezes on an opaque `Uncaught [object WebAssembly.Exception]` with the message gone).
What the host does with a fault is `wgr_guest_set_fault_policy`: log and keep going,
or stop calling the guest and quit. **Still to decide** — wg-vf's rule for a
host-driven op is fatal, on the grounds that a faulted guest's state is untrustworthy.

## Status

Working, at parity with `../simple-hxcpp`: the same scene, the same picking, in a headless
browser — model animating, sprite bobbing, both TTF fonts, music, 60 fps, and the pick
message reading `MODEL PICK: ... pick result y: 0.581142`.

The guest is Haxe compiled to JS (`src/Guest.hx`). It registers its four ops before
`wgr_guest_start` and then drives wgrender's API from inside them.

### Size

Everything a visitor downloads, against the other ports of the same scene (all linked
against the same wgrender web library — `BACKEND=webgl2 WEB_THREADS=0`, release):

| port | wasm | js | total | gzipped | vs C |
|---|---:|---:|---:|---:|---:|
| C (wgrender's example) |   690,116 | 64,910 |   755,026 | 315,188 | 1.00x |
| Nim |   713,584 | 65,138 |   778,722 | 325,062 | 1.03x |
| Beef |   846,131 | 66,674 |   912,805 | 392,828 | 1.21x |
| Haxe, in the wasm (../simple) | 1,685,600 | 69,587 | 1,755,187 | 483,670 | 2.32x |
| Haxe, as a guest (this) |   701,424 | 86,179 |   787,603 | 328,744 | **1.04x** |

The honest headline is **1.04x the C download**, not "less than half of ../simple" —
the interesting number is how close this gets to C, not how far it is from the
all-in-one build. This port's JS breaks down as:

| | bytes | gzipped | |
|---|---:|---:|---|
| `wgrender-host.js` | 67,424 | 24,061 | Emscripten glue |
| `guest.js` | 18,184 | 4,684 | **the whole game, from Haxe** |
| `boot.js` | 571 | 349 | the ESM shim that loads one and starts the other |

So Haxe costs about 4% over writing this in C: 11,308 bytes of wasm for the guest ABI
plus 18 KB of JS for the game, instead of a megabyte of runtime compiled in.

No `index.html` in any row — the page shell is ~8.6 KB and common to the other
ports, while this one's page is a 600-byte hand-rolled file, so counting HTML would
flatter this port for the wrong reason. The Nim row is its `WEB_THREADS=0` build; its
checked-out `out/web/webgl2` is the threaded default (726,858 wasm) and doesn't compare.

### Startup

Size doesn't decide this, and the first attempt was *slower* despite shipping less.
`tools/webstart.py --site=DIR` at the library root (wgrender's own `tools/webstart.py` takes
a web preset's site, with its `examples.json`), median of 3, ms to
`wgr:first-frame`:

| visit | net | simple-js | ../simple |
|---|---|---:|---:|
| cold | local |  362 | 327 |
| cold | 4G    | **731** | 888 |
| warm | local |  129 |  94 |
| warm | 4G    |  414 | 248 |

**Cold on a slow link — the case the payload matters for — the guest route wins by
157 ms.** Everywhere else it's a wash or slightly behind, because it is four files
instead of two and more of the time goes on wiring rather than bytes.

Before the page carried `<link rel=modulepreload/preload>` hints, cold 4G was
**1,040 ms** — worse than `../simple-hxcpp`. `tools/waterfall.py` found why: the module
graph was `html → boot.js → wgrender-host.js → wasm`, so the wasm request didn't start
until 533 ms, against 241 ms for `../simple-hxcpp` (wgrender's shell kicks off
`fetch(wasmUrl)` early, in parallel with its JS). With the hints the wasm starts at
167 ms and cold 4G lands at 731.

Worth keeping in mind: splitting the payload buys nothing unless the page also tells
the browser about every piece up front. Folding `boot.js` into the HTML and bundling
the guest with it would cut two more files, and is the obvious next thing to try for
the warm numbers.

### How much of the binding was reusable

The whole point of the two-layer split. `src/wgr/` is `../simple-hxcpp`'s modules with
target guards applied, and since the split into one module per wgrender header the
answer is visible per file: **15 of 39 modules carry a guard, 24 carry none.**

| guarded | why |
|---|---|
| `Wgr`, `Native` | entering the loop and hxcpp's pointer plumbing — the guest ABI's job here |
| `Asset`, `AssetTask` | the guest gets an id back, not a closure (`Asset.setHost` is shared) |
| `Input`, `KeyboardState` | the mouse struct is read differently; the 512-int keyboard struct isn't marshalled yet |
| `Scene`, `Text` | struct converters: hxcpp unpacks a C struct, the JS layer returns the value |
| `AlignX`, `AlignY`, `Key`, `LightKind`, `LogLevel`, `Projection`, `SpriteFacing` | one `@:to` cast each, only to satisfy C++'s enum parameters |

Everything else — `Model`, `Mesh`, `Sprite3D`, `Texture`, `Font`, `Text2D`, `Text3D`,
`Camera3D`, `Light`, `Sound`, `Audio`, `Render`, `Window`, `Color`, `Handle`, `Vec2`,
`Vec3`, `PickResult`, `MouseState`, `SceneMember`, `Transform` and the flag enums —
compiles for both targets untouched.

What needed guarding was exactly what the host/guest model replaces or what hxcpp
needs specifically:

- the app lifecycle (`initValues`/`setInit`/`setFrame`/`run` and their trampolines) —
  the guest ABI enters the loop instead
- `Asset.ensureAsync`/`AssetTask` — the guest gets an id back, not a closure
  (`Asset.setHost` is shared)
- `Native`, the hxcpp pointer and callback plumbing
- the seven `@:to toRaw()` enum casts, which exist only to satisfy C++'s enum
  parameters
- four struct converters: hxcpp unpacks a C struct, the JS layer already returns the
  Haxe value
- `KeyboardState`, whose 512-int struct the JS layer does not marshal yet

Layout follows librl's: public API flat in `wgr/`, the per-target C surface under
`wgr/impl/` (`Raw.js.hx` / `Raw.cpp.hx`, `GuestAbi.js.hx` / `GuestAbi.cpp.hx`), each
with a plain `.hx` beside it holding an `#error` for an unsupported target — the same
guard `rl/impl/RLImpl.hx` provides. So `import wgr.*;` gives a consumer the API and
none of the plumbing.

`src/wgr/impl/Raw.js.hx` is the other half — 453 calls, written whole by
`tools/gen_raw.py` from wgrender's `include/*.h`, with the handful that need care
declared in that tool rather than patched into its output, so the two ports cannot
drift. `tools/gen_raw.py --check` fails when the headers have moved and the binding
has not.

### What the marshalling layer has to get right

Three measured hazards, all handled in one place:

- **Never hold a heap view.** `ALLOW_MEMORY_GROWTH` detaches every `HEAPF32`/`HEAP32`
  that JS holds on any allocation — measured, a 192 MB `malloc` mid-frame took a
  cached view's `byteLength` to 0 while a fresh read was fine. Every read goes through
  `host.HEAPF32` at the point of use. Pointers survive growth; views don't.
- **Scratch is an arena per op.** Strings and struct-return slots come from
  `stackAlloc`, and `Guest.op` restores the stack pointer once when the op ends, fault
  or not. Haxe has no `finally`, so per-call restore would need a try/rethrow at every
  call site.
- **Structs return through a hidden out-pointer.** Confirmed against the wasm C ABI
  with a standalone test before trusting it: `vec2_t` and a 36-byte struct both came
  back through a first-argument pointer.

## Desktop: the same guest, native

`haxe build.desktop.hxml` builds `Guest.hx` through hxcpp instead of to JS. There is no
wasm and no host module: hxcpp compiles `host/wgr_guest.c` and links wgrender straight
into the binary, so the guest ABI is a set of plain function pointers. Verified
running the same scene with `Platform: desktop`.

The per-target split is `wgr.GuestAbi`, which Haxe picks by target the same way it
picks `Raw`:

| | lines | conditional lines |
|---|---:|---:|
| `src/Guest.hx` — the game | 220 | **0** |
| `src/wgr/GuestAbi.js.hx` | 192 | 0 |
| `src/wgr/GuestAbi.cpp.hx` | 220 | 0 |
| `src/wgr/impl/Raw.js.hx` | 1,723 | 0 |
| `src/wgr/impl/Raw.cpp.hx` | 1,150 | 0 |
| `src/wgr/*.hx` — the shared API | 5,014 | 119 |
| `host/wgr_guest.c` | 214 | 5 |

The game itself now has none at all. The three that used to be there were the asset
base — a served origin on the web, a directory on desktop — and they moved into
`wgr.Assets.defaultBase()`, so the game asks for the default and the binding knows
what that means per target. Everything else — registering the ops,
catching at the edge, the scratch arena, asset loading — is behind `GuestAbi`, so
`Guest.hx` never mentions a target.

### Where the two platforms actually differ

Three places, and no more:

- **Who calls `main`.** On the web nothing in the module can be the entry point, so
  Emscripten gets a stub and the page's boot script registers the guest and calls
  `wgr_guest_start`. On desktop the guest is compiled in and hxcpp supplies `main`, so
  the glue's stub is a duplicate symbol — `host/wgr_guest.c`'s one line of `#ifdef`.
  `GuestAbi.autostart` is the Haxe side of the same fact.
- **How an op becomes a function pointer.** `addFunction` on a JS function, against
  `cpp.Callable.fromStaticFunction` on a static one.
- **Marshalling.** `Raw.js.hx` copies strings into the wasm stack and reads struct
  returns out of the heap; `Raw.cpp.hx` passes them directly. Hence the scratch arena
  on one side and nothing on the other.

Both ops still catch — the reason differs (a frozen page against an exit 255) but the
rule doesn't.

One wrinkle worth recording: the `@:buildXml` that tells hxcpp where wgrender is rides
on `wgr.GuestAbi` here, not on `wgr.Wgr` as it does in `../simple-hxcpp`. This guest never
calls `Wgr`'s lifecycle functions, so `-dce full` strips the class and the metadata
goes with it. Whatever carries the build config has to be something the build keeps.

### Known gaps

- `KeyboardState` isn't marshalled (the other input paths are)
- the guest's ops are wrapped through `Reflect.makeVarArgs`, which is tidy but not the
  cheapest possible edge
- fault policy is still the default (log and continue); see above

## Build

```sh
haxe build.web.hxml                 # out/web/js-webgl2-nothreads: the guest, its host (.js + .wasm), the page
examples/build.py web simple        # the same, with the suite's checks first
examples/build.py sizes simple
examples/build.py drive simple      # headless browser smoke test; writes out/check.png
```

The host exports the guest ABI plus the slice of wgrender's C API the guest calls —
read from `wgrender-hx's src/wgr/impl/Raw.cpp.hx`, so the two ports cannot drift apart (104 calls
today).
