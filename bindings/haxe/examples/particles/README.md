# particles — wgrender's emitters, as a guest app

A port of wgrender's `examples/particles.c`, sibling to [../simple](../simple) and
built the same way: the [wgrender-hx](../../github/whirlinggizmo/wgrender-hx) haxelib,
wgrender as a wasm host on the web, and the same Haxe source compiled native through
hxcpp on desktop.

Three 3D emitters in a scene — a fountain of blended drops under gravity, additive
sparks from a source circling it (thrown along by it, slowed by drag, stretched along
their motion), and a campfire of flipbook flames under curve-driven smoke — plus a 2D
confetti burst on click. Space pauses the steady emitters; O stops and restarts the
camera's orbit.

## Why a second example

`simple` only proved the binding against the API it was designed around. This one uses
a part of wgrender the binding had never seen: `wgr_emitter3d.h`, `wgr_emitter2d.h`,
immediate 3D primitives (`Render.beginMode3D` / `Shape3D.drawGrid`) and
`Debug.enableFps` — 68 distinct calls, about 30 of them new.

It went in without changing the binding's shape. The emitters became two more handle
abstracts and one more `enum abstract`, both `SceneMember`s, and the hxcpp externs came
out of the headers by parser rather than by hand — 72 of 76 unattended.

## Build

```sh
haxe build.web.hxml                   # the JS guest and its host wasm, into out/web/js-webgl2-nothreads
examples/build.py all particles       # that, plus the native guest
examples/build.py desktop particles   # out/linux/release/particles-guest, with assets linked beside it
examples/build.py sizes particles
examples/build.py drive particles     # headless smoke test; clicks once for the confetti
```

## Size

| | bytes | gzipped |
|---|---:|---:|
| host wasm | 730,935 | 307,641 |
| host js (Emscripten glue) | 69,829 | 24,501 |
| guest js — the whole example | 19,989 | 4,078 |
| **total** | **820,753** | |

The host is ~30 KB of wasm larger than `../simple`'s, which is the particle subsystem
being linked in; the guest is about the same size as `simple`'s, because it is about as
much code.
