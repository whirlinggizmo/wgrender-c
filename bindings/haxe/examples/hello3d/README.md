# hello3d, in Haxe

A port of wgrender's `examples/hello3d.c`: an orbiting `Camera3D` over immediate-mode
3D primitives with a 2D text overlay. wgrender runs as a wasm host and this is a JS
guest on top of it; the same source also builds native through hxcpp.

It loads nothing, which makes it the size floor for this architecture — what a Haxe
game costs over the wgrender host when the game itself is seventy lines.

    haxe build.web.hxml                 the guest and its host, into out/web/js-webgl2-nothreads
    haxe build.desktop.hxml             the native binary
    examples/build.py serve hello3d     http://localhost:8000/hello3d/
    examples/build.py drive hello3d     headless smoke test

## Against the C

Same wgrender, same backend, same threading (`WEB_THREADS=0`), so the only difference
is the language:

| | wasm | js | total | gzipped |
|---|---|---|---|---|
| C | 241,738 | 60,128 | 301,866 | 129,360 |
| Haxe | 241,963 | 61,440 + 8,954 | 312,357 | 132,509 |

1.03x raw, 1.02x gzipped. The wasm is the same wgrender either way; the 8,954 bytes
of `hello3d.js` are the game. Frame cost differs by about 0.03 ms of script time,
against a 16.7 ms budget.

The host is 242 KB here against 689 KB for `simple`, because it exports exactly the
25 wgrender calls this guest makes and Emscripten drops the rest.
