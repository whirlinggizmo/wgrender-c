# wgrender-hx

Haxe bindings for [wgrender](https://github.com/whirlinggizmo/wgrender-c), over two
targets from one API:

- **hxcpp** — native desktop, or compiled into the wasm alongside wgrender
- **js** — wgrender is a wasm *host* and your game is a guest module, so the Haxe
  runtime never enters the binary

**The examples run in a browser: https://whirlinggizmo.github.io/wgrender-hx/** —
every one as a JS guest, published from `main` by `.github/workflows/pages.yml`, with
each one's size against wgrender's own C build of it. No threads (GitHub Pages can't
send the headers a threaded page needs), WebGL2.

```haxe
import wgr.*;

final mesh = new Mesh(path);
model = new Model(mesh);      // wgrender's rule: object from resource, resource from path
mesh.release();               // the model holds its own reference
model.setAnimationLoop(true);
model.setTint(Color.RAYWHITE);
scene.add(model);             // takes a Model, Sprite3D, Text2D, Text3D or Light

if (pick.handle == model) ... // the untyped pick handle still compares to typed ones
```

## Layout

```
src/wgr/*.hx          the API — one module per wgrender public header
src/wgr/GuestAbi.hx   installing the guest's ops; .{cpp,js}.hx per target
src/wgr/import.hx     gives those modules the C surface (not in macro context)
src/wgr/macros/       compile-time code, run from an hxml: WebHost links a guest's host;
                      Cppia is check-cppia's module side ("Calling it from cppia")
src/wgr/impl/         the generated C surface, chosen by target. Nothing outside the
                      binding imports from here; an app needs `import wgr.*` and no more
  Raw.hx                #error for a target with no implementation
  Raw.cpp.hx            hxcpp externs against wgr.h
  Raw.js.hx             calls the host module's exports, and marshals
  GuestRaw.cpp.hx       externs for bindings/host/wgr_guest.h, which is this binding's own C
bindings/host/wgr_guest.{c,h}  the guest ABI: wgrender as a host, five ops
examples/             the guests, and simple-hxcpp the other way (all-in-one)
project/              how a build compiles wgrender (the repository two directories up)

tools/check_binding.py         the binding's checks: headless, js, cppia, the lists, the C
test/CheckBindings.hx runs against headless wgrender and asserts what comes back
test/Cppia{Host,Module}.hx  check-cppia: every public function, called from cppia

tools/gen_raw_externs.py      writes BOTH impl/Raw.*.hx whole, from wgrender's include/*.h
tools/gen_keys.py     regenerates wgr.Key from wgrender's wgr_keys.h
tools/check_coverage.py     what the binding reaches, and what wrapping next buys;
                      --check fails if a C call has two names (tools/members.py
                      is the member index it and check_refusals.py share)
tools/check_refusals.py     every way a wgrender call can return false, read from the C
                      with clang; --check fails if a documented refusal is not
                      repeated in these docs
tools/guestbuild.py   the examples' suite runner: checks, desktop assets, sizes, serve
tools/compare_sizes.py      each example's size against wgrender's own C build of it
tools/run_benchmarks.py   size, frame cost, GC and call cost against the C -> docs/benchmarks.md
tools/build_hxcpp_example.py     an example all-in-one through hxcpp for the web, for the benchmarks
tools/drive_example.py        run a built example and fail on anything the console calls an error
tools/measure_example_startup.py     startup timing of any web build (--site=DIR); tools/show_waterfall.py, its requests
tools/wgrweb.py       what the browser checks share: weblib, the wgrender they run against, serving
web/index.html        the page for the all-in-one hxcpp builds (the JS guests' pages are WebHost's)
```

Every operation is a static named after the C call it makes, taking the handle first:
`wgr_model_set_tint` is `Model.setTint`, `wgr_model_is_visible` is `Model.isVisible`,
`wgr_window_has_fullscreen` is `Window.hasFullscreen`. Some names are Haxe's rather
than C's — `Mesh.cube` for `wgr_mesh_create_cube`, `Font.draw` for `wgr_text_draw_ex`
— but every C call has exactly one member, and a member that calls C calls one C
function. That is what lets the binding be audited mechanically
(`tools/check_coverage.py --check` holds the rule, `tools/check_refusals.py --check` reads each
member's docs) and what keeps a second binding in step — a property has no counterpart
in Lua or Nim, and it cannot return the `Bool` a wgrender setter uses to refuse.

Anything more is sugar over those members, never a second path to C:
`Version.runtime` builds its string from `major`, `minor` and `patch`, and the two
rounded rectangles are overloads of one `Shape2D.drawRoundedRectangle`.

The sugar you will use most is how those statics are called. Every handle kind is
declared `@:using` itself, so a static whose first parameter is that kind is also a
method on it, with no `using` or import at the call site: `model.setTint(c)` is
`Model.setTint(model, c)`, the same inline call. And each kind with a plain `create`
has a constructor that calls it, so `new Model(mesh)` is `Model.create(mesh)`. It is
still a handle, not an object the GC owns: `destroy` or `release` it as before. The
examples use these forms; the statics remain, and are what the audits read.

A member that only reads struct data keeps its shape, because there is no C name to
mirror: `Vec3.x`, `MouseState`, `KeyboardState.isPressed`, `Handle.isNone`.

Anything with a transform has the same calls for it, as in C: `setTransform` with every
part it has (none of them optional), and `setPosition`, `setRotation`, `setScale` and
their getters for one part at a time, which leave the others as they are. So moving a
model is `model.setPosition(p)`, with no need to know or keep its rotation and
scale; `setTransform` is the one call a frame for something whose parts all change.
Pass `Vec3.ZERO` or `Vec3.ONE` for a part that isn't turned or scaled. Each one-part
setter, and a pivot or an emitter's `jump`, also takes the components, as Nim's
overloads do: `model.setPosition(x, y, 0)` is `model.setPosition(new Vec3(x, y, 0))`,
the same C call, which takes three floats either way. Neither allocates -- a
`new Vec3(...)` handed to an inline call compiles away -- so the split form is for the
reader, not the frame. A getter is where a vector is real: on js it reads C's
`vec3_t` into a new `Vec3` on every call.

Every handle kind is an `abstract` over `Int` and every member is `inline`, so the API
layer compiles away: a call costs what the C call costs. The only allocations are the
small value objects (`Vec2`, `Vec3`, `MouseState`, `PickResult`) the wrappers return.
The typed abstracts are free too, and they earn their place twice: a `Mesh` where a
`Texture` belongs is a compile error, and so is a bare literal `0` where `Handle.NONE`
is accepted — which makes "0 is a value you pass on purpose" enforceable rather than
documented. See `docs/handles.md`.

### Why handles aren't `null`

A handle is 0 when it refers to nothing, and `isNone` reports that — rather than the
`model != null` a Haxe developer would reach for first — `model.isNone()`, or
`h.isNone` on an untyped `Handle`.

0 is a value you *pass*, not only one you get back. `new Model(Handle.NONE)` is an
empty model that joins the scene immediately and is given its mesh when the asset
arrives, so the frame loop never asks whether it has loaded — which is how `model`,
`materials`, `lights`, `sprite2d` and `text3d` are all written, following the C.
So `isNone` means "empty", not "invalid", and that is why it is not called `isValid`.

Using 0 rather than `null` is deliberate, and measured:

- **`null` is not 0.** On hxcpp `Null<Int>(0) == null` is `false`, so the two would
  have to be mapped at every boundary, in both directions.
- **`Null<Model>` stops being an `Int`.** hxcpp renders it as `::Dynamic` — boxed, and
  no longer inlinable, which is the whole point of the layer.
- It would only be free on js, where `null` is native.

A field needs no initialiser either way: `isNone` treats js's `undefined` as none, so
`static var model:Model;` is correct on both targets. Without that it would be correct
on hxcpp (an uninitialised `Int` static is 0) and quietly wrong on js (`undefined == 0`
is `false`).

**`isNone` is the only test that is right on both targets.** `h == 0` and
`h == Handle.NONE` compile, and are right on hxcpp and right on js for a field that was
assigned — but both say `false` for an uninitialised field on js, for the same
`undefined == 0` reason. Measured, not reasoned:

| on js | uninitialised | assigned `Handle.NONE` |
|---|---|---|
| `h == 0` | `false` | `true` |
| `h == Handle.NONE` | `false` | `true` |
| `h.isNone` | `true` | `true` |

The 0 is promoted at the typed abstract (`Int` → `Handle` → `Model`), so an operator
overload on `Handle` never sees the comparison and cannot correct it.

Note what this does *not* argue. The abstract does not prevent `== 0` — the table above
is measured with it in place. It only makes `isNone` discoverable, because `h.` offers
it. The flat API carries the same check as a static, `Model.isNone(model)`, with the
same `undefined` tolerance inside it; flattening lost the autocomplete, not the
correctness, and `@:using` gives the autocomplete back.

## The two shapes

An **all-in-one** app calls `Wgr.initValues` / `setInit` / `setFrame` / `run` and is
compiled into the binary (or the wasm) with wgrender. A **guest** app implements
`bindings/host/wgr_guest.h`'s ops — `init`, `frame`, `asset`, `shutdown`, and an optional
fixed-rate `tick` — and the host
calls them; on js the host is a wasm module the page loads, on hxcpp it is linked in
and the Haxe program's `main` is the entry point.

The `@:buildXml` carrying the link configuration rides on `wgr.impl.Raw`, because
every program that touches wgrender at all reaches that class — anything in the API
layer can be stripped by `-dce full` out from under the build.

A handful of the API modules carry a target guard; the rest compile for both
untouched.

## Calling it from cppia

Hot reload ([hotreload-hx](https://github.com/whirlinggizmo/hotreload-hx)) runs an
application's reloaded code as a cppia module in the executable, and that code calls
this binding. cppia can't run an extern call or `__cpp__`, so the module doesn't
inline the binding's wrappers: it calls the executable's compiled copies of them
(hotreload-hx arranges that for every library). Two rules follow for how a wrapper is
written, and `tools/check_binding.py` holds the whole binding to both (check-cppia: a
`-D scriptable` executable with every module of the binding in it, and a cppia module
that calls every public function once, generated by `wgr.macros.Cppia`, loaded into it):

- **A wrapper is `inline`, never `extern inline`.** hxcpp compiles a copy of an
  `inline` function, which is what cppia calls; an `extern inline` one has no copy,
  so its body is inlined into the module, and if that body is the C call, the reload
  fails to load (`Unknown static call to wgr.impl.Raw::...`). Haxe makes every
  `overload` function `extern inline`, so an overload doesn't make the C call: it
  forwards to the one `inline` member that does, which the native build inlines all
  the same.

  ```haxe
  public static overload extern inline function setPosition(model:Model, value:Vec3):Bool
  	return setPosition3(model, value.x, value.y, value.z);
  public static overload extern inline function setPosition(model:Model, x:Float, y:Float, z:Float):Bool
  	return setPosition3(model, x, y, z);
  static inline function setPosition3(model:Model, x:Float, y:Float, z:Float):Bool
  	return Raw.wgr_model_set_position(model, x, y, z);
  ```

- **A C type never reaches a compiled function of a public class.** `-D scriptable`
  gives every static of a public class a wrapper cppia calls it through, with
  `Dynamic` arguments, and for a C pointer, struct, enum or function pointer
  (`ConstCharStar`, `VoidStar`, a `CVec3`, a `CTextAlign`, a `cpp.Callable`) that
  wrapper doesn't compile. So a helper with one in its signature is `extern inline`
  (`Native.cstr`, `Vec3.of`, an enum abstract's `toRaw`), and a C callback, which has
  to be a compiled function, lives in a private class (`AssetNative`, `GuestAbiNative`).
  An abstract's implementation class is private already, so an abstract is free.

Everything else about the binding's shape — abstracts over `Int`, `@:using`, enum
abstracts with `@:to` casts, `@:structAccess` externs — is fine as it is.

## Logging

`Log.info(msg)` reaches wgrender's logger through `wgr_logger_message_source`, with
the file and line filled in by the compiler from `haxe.PosInfos` — so a line reads
`[INFO ] Guest.hx:91: ...`, naming the Haxe call site rather than generated C++.
`Log.plain(level, msg)` is the bare form.

The message is handed over as a `%s` argument, never as the format string itself, so
text can't be read as a format directive and nothing has to be escaped. On js, a log
before the host module is attached falls back to `console.log` rather than throwing —
startup going wrong is exactly when you want the log.

## Assets

`Assets.defaultBase()` resolves where assets load from at run time, rather than from a
compile-time define, so a built program is relocatable and a development build uses
the same lookup a shipped one does:

| | |
|---|---|
| web (js or hxcpp/Emscripten) | `/assets` — what `tools/serve_site.py` mounts, and what a host should serve |
| native | `$WGR_ASSET_BASE`, then an `assets` directory beside the executable, then `assets` relative to the working directory |

wgrender links no HTTP and no TLS, so a miss on a native build is a question it asks
the program: `Asset.setFetcher`. The binding ships the answer —
`Asset.setFetcher(Asset.httpFetcher)` installs `haxe.Http` over hxcpp's bundled
mbedtls, verifying certificates from the system store. It costs about a megabyte of
binary and only if you name it: a program that never installs a fetcher is byte for
byte the size it was, measured. On the web there is nothing to install and
`setFetcher` answers `false`, because the browser is already the downloader.

An app's build is expected to put `assets` next to the built executable — the examples
link it to wherever wgrender is checked out.

## Using it

What to install on each OS, and every build, example and check: [BUILDING.md](BUILDING.md).

```sh
haxelib git wgrender-hx https://github.com/whirlinggizmo/wgrender-c main bindings/haxe
```

then `-lib wgrender-hx`. That is the whole install: `haxelib git` clones wgrender-c
and makes `bindings/haxe` the library's root, so wgrender is two directories up with
its sources and vendored dependencies, and `project/Build.xml` hands hxcpp the include
paths and the C files to compile. There is no library to build first and nothing to
point at by hand, because hxcpp compiles wgrender with the same toolchain it compiles
your program with. A web build links wgrender's web library the same way, building it
first when it isn't there.

**On Windows, add `-D HXCPP_M64`.** hxcpp builds 32-bit there unless told otherwise,
which is rarely what a game wants. The examples' `build.desktop.hxml` files carry the flag; it does nothing on Linux or macOS,
which build 64-bit anyway.

Working on the binding itself, from a wgrender-c checkout:

```sh
haxelib dev wgrender-hx bindings/haxe
```

A checkout's build and an install's are the same: both go through `project/Build.xml`,
which compiles the wgrender the binding sits in, so a checkout is not exercising a path
nobody else runs. A published haxelib zip can't reach `../..`; packaging one will need
its own step that carries wgrender's sources.

`project/wgrender.xml` carries wgrender's own flags for each build hxcpp can make:
native per OS, `-D wgr-headless` for its headless build (no window, GPU or audio:
`tools/check_binding.py` uses it), and hxcpp's emscripten target for the web (webgl2, or
`-D wgr-webgpu`; `--debug` for wgrender's debug flags).

`-D WGR_BUILD_XML=<file>` still exists for a build that needs more than a path — it
names an hxcpp build-tool XML outright, and wins over everything above. Nothing in
this repository uses it.

### A web guest, from your own hxml

A JS guest needs a wasm host beside it: wgrender compiled with Emscripten, exporting the
calls the guest makes. One line in the section that builds your guest does that:

```
-lib wgrender-hx
--main Game
--js out/wasm32/release/site/game.js
--macro wgr.macros.WebHost.build()
```

It builds wgrender's web library if it has to, links `wgrender-host.js` and
`wgrender-host.wasm` next to your `--js` output, and writes `boot.js` and an
`index.html` to load them. `boot.js` is regenerated every build, since it has to match
the host; `index.html` is written once and is yours after that. The line can sit
anywhere in the section — Haxe reads the whole section before it runs the macro.

The host exports exactly what your guest calls, which is about a quarter smaller
gzipped than exporting the whole binding. To find out what that is, the macro compiles
your program a second time in a child `haxe`, with your own arguments plus
`-dce full -D no-inline --no-output --json`: with inlining off, dead-code elimination
leaves the `wgr.impl.Raw` wrappers you reach as declarations, and `--json` lists them.
So your own build is untouched — `-dce no --debug` is fine — and nothing is read out of
generated JavaScript. `-D no-inline` can only ever list more than you need, never less,
so the worst case is a slightly bigger wasm. The child takes a fraction of a second,
and the link is skipped when the list, the flags, wgrender's library and the host glue
are all unchanged.

- `-D wgr-host=full` exports the whole binding and skips the child compile. Good for
  development, since the host then only relinks when wgrender changes, and the way to
  rule the listing out if something misbehaves.
- `-D wgr-build-dir=<dir>` is where linked hosts are cached; by default
  `build/wasm32-<variant>/webhost` (`build/wasm32-release/webhost`, ...).
- `-D wgr-title=<text>` and `-D wgr-background=<css colour>` shape the first `index.html`.
- `WEB_THREADS`, `BACKEND` and `WEB_DEBUG` in the environment mean what they mean to
  wgrender's own web build.

It needs Emscripten's `emcc` on the path, and nothing else: wgrender's web library is
built by its own `tools/build_web_library.py`, on the Python emsdk brings, from its
`build.json`, so there is no build tool or shell to install, on Windows either. A call into wgrender made only
through reflection is invisible to dead-code elimination and will not be listed; mark
its caller `@:keep`. On a native target the line does nothing, so a shared hxml can
carry it.

### The examples

```sh
tools/run_examples.py all      build each one, web and native
tools/run_examples.py drive    run each web build in a headless browser
tools/run_examples.py site     collect them under one page, with wgrender's assets: any static host
tools/run_examples.py compare  sizes against wgrender's own C build of each
tools/check_binding.py              the binding's own checks, against headless wgrender
```

Each example is what you would write yourself: `src/`, a `build.web.hxml` and a
`build.desktop.hxml`. Those files *are* the build — `haxe build.web.hxml` in an
example's directory gives you its `out/wasm32/release/site`, host and page included — so copying an
example is how to start a project. There is nothing else in an example to copy or to
ignore: `tools/run_examples.py` does the suite's chores for each one by name — it checks the
binding is the one you are working on and current, puts wgrender's sample assets beside a
desktop binary, and serves every example from one server, each in its own
subdirectory. Name examples to limit any command: `tools/run_examples.py serve model`.

Thirty-two of wgrender's 33 C examples are ported — all but `clay`, whose API is C
macros over a C layout library that a JS guest cannot reach — each named after the C
file it ports and keeping its numbers, keys and on-screen text. `tools/run_examples.py list` prints them
with a line each. One more, [`stress`](examples/stress), ports wgrender's benchmark scene
(`tools/bench/stress.c`) rather than an example: thousands of entities for
`tools/run_benchmarks.py` to measure. The ones to read first:

- [`hello`](examples/hello) and [`hello3d`](examples/hello3d) — the smallest; hello3d
  loads nothing at all, which makes it the size floor
- [`simple`](examples/simple) — a model, a sprite, text, audio and picking, and the one
  the size and frame-cost tables are measured on
- [`tilemap`](examples/tilemap) — a scrolling 2D tile map with no Camera2D, which is the
  point: sprite3d in the XY plane under an orthographic camera keeps the 3D scene,
  layers and picking
- [`fetch`](examples/fetch) — the one place the two targets genuinely differ rather
  than differing at the edges, since `Asset.setFetcher` is hxcpp-only and the web has
  the browser
- [`materials`](examples/materials) — every material kind at once, all of them
  assigned before the mesh they belong to has loaded
- [`environment`](examples/environment) — image-based lighting with no lights in the
  scene at all, plus background blur and tone mapping
- [`instancing`](examples/instancing) — 400 cubes that go up as one draw, and nothing
  in the source asks for it
- [`shaders`](examples/shaders) — four custom shaders, including a toon one on a
  *skinned* model and water that moves its own vertices

[`simple-hxcpp`](examples/simple-hxcpp) is `simple` built the other way, for the size
comparison against the C, Nim and Beef ports.

## Minifying the web build

Plain minification of the JS output (esbuild `--minify`, terser's defaults) is safe.
Under **property** mangling (esbuild `--mangle-props`, terser `mangle.properties`,
Closure's advanced mode) the binding stays safe: every name it sends to the wasm host is
a quoted key (`Raw.host["_wgr_..."]`, `Raw.host["HEAPF32"]`), which manglers leave alone,
and `Raw.host` is a `haxe.DynamicAccess`, so a dotted access that a mangler could break
doesn't compile. V8 compiles a constant quoted key exactly as a dotted one, so it costs
nothing. Your own code needs nothing special: `getPosition` and the other vector getters
return the binding's `Vec3`, whose fields a mangler renames consistently within your
output.

Don't minify Haxe's output with esbuild `--format=esm`: esbuild reads Haxe's module
wrapper (`typeof exports != "undefined" ? exports : window`) as CommonJS, and the guest's
`@:expose`d entry point never reaches the page (`WgrGuest` is undefined). Without
`--format` it works.

Measured (2026-10-03, esbuild 0.28, the stress scene with 5,000 position reads a frame,
the output minified with `--mangle-props=^(_|HEAP|stack|wgr_|x$|y$|z$)`): runs, and the
positions read back right.

## Status

`wgr.impl.Raw` covers all 480 of wgrender's calls on hxcpp and 470 on js; the
hand-written API layer above it wraps 478. All 33 of wgrender's C examples could be
written against it without a gap.

The two it leaves alone are a decision, not a backlog: `wgr_text_draw_n` and
`wgr_text_measure_n` take a length in *bytes*, and a Haxe string measures in UTF-16
units, so the two disagree for anything non-ASCII — passing a substring to `draw()` is
correct and these would not be. `tools/check_coverage.py --check` fails if either is ever
wrapped after all, so a decision and a to-do stay distinguishable. It holds the js
omissions the same way: six calls that take a C function pointer, which the guest ABI
replaces there.

`tools/check_refusals.py --check` guards the other direction. wgrender's headers name every value a
setter refuses, and this fails the build when one of those sentences is not repeated
in the binding's docs — the link that broke once already, when a header's "capped at
65536" was copied into a doc comment and the API shape followed the doc rather than
the code.

### Against the C

Same wgrender, same backend, same threading, so the only difference is the language:

| example | C | Haxe | vs C | gzipped | guest js |
|---|---|---|---|---|---|
| hello3d | 301,866 | 312,357 | 1.03x | **1.02x** | 8,954 |
| particles | 473,289 | 494,452 | 1.04x | **1.01x** | 21,996 |
| simple | 757,457 | 779,052 | 1.03x | **1.01x** | 21,686 |

The wasm is the same wgrender either way; the difference is the guest JS. Frame cost,
JS heap and GC, and what a call across the boundary costs are in
[docs/benchmarks.md](docs/benchmarks.md), which `tools/run_benchmarks.py` measures with
wgrender's harness against wgrender's C baseline. On `simple`, 16 calls a frame cross
into the wasm; what they cost is lost in the noise of the frame.

For contrast, the same scene compiled all-in-one through hxcpp is 1.69 MB of wasm,
2.3x the C. Keeping the Haxe runtime out of the binary is what the guest shape buys.

Those are `WEB_THREADS=0` on both sides, which is what the examples build: a threaded
build only starts on a cross-origin isolated page, and that needs COOP/COEP headers
that `tools/serve_site.py` sends and a plain static host does not. `WEB_THREADS=1` builds
the other one, and it works -- `simple` reports its four asset loading workers on the
web, where the single-threaded build loads on the main thread and blocks the frame it
happens on. It costs a flat ~21 KB (~9 KB gzipped) whatever the example, since it is
the pthread runtime rather than anything proportional, and the ratios against C are
unchanged at 1.01-1.02x gzipped: threads are wgrender's cost, paid the same either
way.

| threaded | C | Haxe | vs C gzipped |
|---|---|---|---|
| hello3d | 322,581 | 333,256 | 1.02x |
| particles | 494,432 | 515,776 | 1.01x |
| simple | 779,212 | 800,981 | 1.01x |

## Regenerating the C surface

`tools/gen_raw_externs.py` reads wgrender's `include/*.h` and writes **both** `impl/Raw.cpp.hx`
and `impl/Raw.js.hx` whole — neither is ever patched, so the answer to an API change is
to run it and read what it says:

```
$ tools/gen_raw_externs.py
wgrender-c: 480 functions, 18 enums, 12 structs
  Raw.cpp.hx  478 externs
  Raw.js.hx   467 wrappers
```

**hxcpp reaches every one of wgrender's public functions.** On js it reaches all but
four, and those four take a C function pointer: the lifecycle setters, which the guest
ABI's ops replace.

Nothing else in wgrender calls back: what may wait (a resource, an ensure, a ping) is
a handle whose status the program reads, which crosses on both targets as any other
call does.

`wgr_input_get_keyboard_state` used to be an eleventh. Its struct is 2,324 bytes — 512
ints of key state plus the keys and characters a frame produced — and the generator's
only shape for a returned struct was to read every field and build the Haxe class,
which means marshalling 581 ints to answer one question about one key. So it learned a
second shape: a struct listed in `OPAQUE` comes back to js as a pointer into the wasm
heap, with the field offsets emitted beside it, and `keys[Escape]` is one `HEAP32`
index. On hxcpp it still wraps the struct, so the two targets differ by a line per
accessor and the public API is identical.

The tool derives the enum cast types, the struct externs and the struct-return heap
reads from the headers. It needs help for four things, declared in its `SPEC` rather
than edited into its output: which C callback typedefs map to which Haxe function
type, which returned structs map to which public value class (whose constructor must
take the C fields in order), which are too big to copy and come back as a heap
pointer, and which functions to skip. Anything it can't map is
left out and **listed**, so a gap is reported rather than silent. Today the only entry
is the two varargs loggers, which it re-adds at fixed arity from `MANUAL`.

Because the binding now declares wgrender's whole API, an app's build derives the
host's `EXPORTED_FUNCTIONS` from the calls its *compiled guest* makes rather than from
the binding — Emscripten cannot strip what is exported, and exporting everything cost
75 KB. The `hello3d` example exports 25 and its host wasm is 242 KB against `simple`'s
689 KB, because it links no model, glTF, audio or particle code at all. The guest fault policy
defaults to log-and-continue.

## License

MIT; see [LICENSE](LICENSE). wgrender, the repository this lives in, is MIT too, and the
libraries it vendors (sokol, clay, cgltf, stb and others) keep their own licenses, listed
in [its README](../../README.md#license). The example assets
come from wgrender as well; their credits and licenses are in its
[examples/assets/CREDITS.md](../../examples/assets/CREDITS.md).
