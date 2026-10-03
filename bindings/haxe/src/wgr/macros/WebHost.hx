package wgr.macros;

#if macro
import haxe.Json;
import haxe.io.Path;
import haxe.macro.Compiler;
import haxe.macro.Context;
import sys.FileSystem;
import sys.io.File;
import sys.io.Process;

using StringTools;
#end

/**
	The wasm host for a Haxe guest, from one line in your own hxml:

	```
	--macro wgr.macros.WebHost.build()
	```

	Put it anywhere in the section that builds your guest to JS. It links
	`wgrender-host.js` and `wgrender-host.wasm` next to your `--js` output, exporting
	exactly the wgrender calls your guest makes, and writes a page to load them. Your own
	build is untouched: keep `-dce no --debug` or whatever else you like.

	How it knows what your guest calls: it compiles your program a second time, in a
	child `haxe` with your own arguments plus `-dce full -D no-inline --no-output --json`.
	With inlining off, DCE leaves exactly the `wgr.impl.Raw` wrappers your code reaches as
	declarations, and `--json` lists them — documented compiler flags, read as data, no
	scanning of generated text. `-D no-inline` can only make the list larger than the real
	build needs (a branch inlining would have folded away survives), so the error, if any,
	is a slightly bigger wasm, never a missing export. The child costs a fraction of a
	second and writes nothing but the list.

	Linking is skipped when nothing it depends on has changed — the export list, the
	flags, wgrender's library, and this binding's host glue.

	Options, as defines:
	- `-D wgr-host=full` exports the whole binding and skips the listing compile. A
	  fallback, and the way to rule this out when something misbehaves.
	- `-D wgr-host=none` leaves the host and page as they are: nothing built, linked or
	  copied. For a guest rebuilt while a page runs it, whose host is already loaded (a
	  hot reload, say).
	- `-D wgr-build-dir=<dir>` where the linked host is cached (default
	  `build/wasm32-<variant>/webhost`, e.g. `build/wasm32-release/webhost`, named as
	  wgrender's builds are).
	- `-D wgr-title=<text>` and `-D wgr-background=<css colour>` for the generated page.

	Environment: `WEB_THREADS` (default 0), `BACKEND` and `WEB_DEBUG`, as for wgrender's
	own web build. Needs Emscripten (`emcc`) on the path, and nothing else: wgrender's
	web library is built by its `tools/build_web_library.py`, on the Python emsdk brings
	(`EMSDK_PYTHON`), from its `build.json`. No make, no shell, so the same on Windows.

	A reflection-only call into wgrender (`Reflect.callMethod` on `Raw`) is invisible to
	DCE, so the listing will not have it; mark the caller `@:keep`.
**/
class WebHost {
	#if macro
	/**
		The host glue (`bindings/host/wgr_guest.h`), which every guest needs whatever it calls. It
		is our own contract and a handful of small functions, so it is listed rather than
		derived; `tools/check_binding.py` fails if the header gains or loses one.
	**/
	public static final GUEST_ABI = [
		"wgr_guest_faulted", "wgr_guest_frame_id", "wgr_guest_install",
		"wgr_guest_register", "wgr_guest_register_tick", "wgr_guest_set_fault_policy",
		"wgr_guest_start", "wgr_guest_tick_fraction"
	];

	/**
		What the binding's JS reaches on the Emscripten module besides the exports.
		`tools/check_binding.py` fails if `src/wgr` starts reaching one that is not here.
	**/
	public static final RUNTIME_METHODS = [
		"addFunction", "stringToUTF8", "lengthBytesUTF8", "UTF8ToString",
		"stackAlloc", "stackSave", "stackRestore", "HEAPU8", "HEAP32", "HEAPU32", "HEAPF32"
	];

	/**
		C library functions the host always exports, which the binding's JS calls on the
		module as `_name`: the allocator, for the fixed slots structs come back through
		(Raw.js.hx's `record`, `opaqueSlot`). `tools/check_binding.py` fails if the JS
		reaches one that is not here.
	**/
	public static final LIBRARY_EXPORTS = ["malloc", "free"];

	static inline final LISTING = "wgr-listing";
	#end

	public static function build() {
		#if macro
		if (Context.defined(LISTING))
			return; // we are the listing compile this macro spawned
		if (define("wgr-host", "") == "none")
			return; // the host is already loaded: only the guest is wanted
		if (!Context.defined("js")) {
			Sys.println("WebHost: not a JS build, nothing to do");
			return;
		}
		final js = Compiler.getOutput();
		final site = Path.directory(js);
		final name = Path.withoutExtension(Path.withoutDirectory(js));
		final binding = bindingRoot();
		final wgrender = findWgrender(binding);
		final web = webSettings();
		final state = define("wgr-build-dir", 'build/wasm32-${webVariant(web)}/webhost');

		run(python(), [Path.join([wgrender, "tools/build_web_library.py"])].concat([for (k => v in web) '$k=$v']));
		final flags = webFlags(wgrender, web);

		final full = define("wgr-host", "") == "full";
		final api = full ? bindingExports(binding) : listExports(state);
		final exported = ["_main"].concat([for (n in dedupe(GUEST_ABI.concat(api))) '_$n']).concat([for (n in LIBRARY_EXPORTS) '_$n']);

		final lib = Path.join([wgrender, flags.lib]);
		final glue = Path.join([wgrender, "bindings/host/wgr_guest.c"]);
		final header = Path.join([wgrender, "bindings/host/wgr_guest.h"]);
		final out = Path.join([state, full ? "host-full" : "host"]);
		final stamp = [
			exported.join(","), RUNTIME_METHODS.join(","), flags.ldflags.join(" "),
			lib + "@" + mtime(lib), glue + "@" + mtime(glue), header + "@" + mtime(header)
		].join("\n");
		final stampFile = Path.join([out, "stamp.txt"]);
		final what = full ? "the whole binding" : '${api.length} wgrender calls';

		if (FileSystem.exists(Path.join([out, "wgrender-host.wasm"])) && FileSystem.exists(stampFile)
			&& File.getContent(stampFile) == stamp) {
			Sys.println('WebHost: host up to date ($what)');
		} else {
			Sys.println('WebHost: linking the host ($what)');
			FileSystem.createDirectory(out);
			run("emcc", ["-O2", '-I$wgrender/include', '-I$wgrender/bindings/host'].concat(flags.cflags).concat([glue, lib]).concat(flags.ldflags).concat([
				"-sALLOW_TABLE_GROWTH=1", // the guest installs its ops as JS functions turned into C pointers
				"-sMODULARIZE=1",
				"-sEXPORT_ES6=1",
				"-sEXPORT_NAME=createWgrHost",
				'-sEXPORTED_RUNTIME_METHODS=${RUNTIME_METHODS.join(",")}',
				'-sEXPORTED_FUNCTIONS=${exported.join(",")}',
				"-o", Path.join([out, "wgrender-host.js"])
			]));
			File.saveContent(stampFile, stamp);
		}

		FileSystem.createDirectory(site);
		for (f in ["wgrender-host.js", "wgrender-host.wasm"])
			File.copy(Path.join([out, f]), Path.join([site, f]));
		// An experiment (-D wgr-js-binding, with gen_raw_externs.py --via-js's Raw.js.hx on
		// the class path): the calls go through the JS binding, which the page loads too.
		final viaJs = Context.defined("wgr-js-binding");
		if (viaJs) {
			FileSystem.createDirectory(Path.join([site, "src"]));
			File.copy(Path.join([wgrender, "bindings/js/wgrender.js"]), Path.join([site, "wgrender.js"]));
			File.copy(Path.join([wgrender, "bindings/js/src/runtime.js"]), Path.join([site, "src/runtime.js"]));
		}
		// boot.js is glue that has to match the host, so it is always ours. The page is
		// yours once it exists: written only when missing, never overwritten.
		writeIfChanged(Path.join([site, "boot.js"]), viaJs ? bootJsViaJs(name) : bootJs(name));
		final page = Path.join([site, "index.html"]);
		if (!FileSystem.exists(page))
			File.saveContent(page, indexHtml(name, define("wgr-title", name), define("wgr-background", "#101218")));
		Sys.println('WebHost: host and page -> $site');
		#end
	}

	#if macro
	/**
		The wgrender calls this guest makes, from a child compile of the same program.

		The child gets this section's own arguments, which `Sys.args()` returns already
		expanded from any hxml, plus the listing flags. Later flags win, so its `-dce full`
		overrides yours there without touching your build. `--cwd` is dropped: the child
		inherits the directory that flag already moved us to.
	**/
	static function listExports(state:String):Array<String> {
		final compiler = compilerPath();
		FileSystem.createDirectory(state);
		final list = FileSystem.fullPath(state) + "/listing.json";
		if (FileSystem.exists(list))
			FileSystem.deleteFile(list);

		final args = [];
		final given = Sys.args();
		var i = 0;
		while (i < given.length) {
			if (given[i] == "--cwd" || given[i] == "-C") {
				i += 2;
				continue;
			}
			args.push(given[i++]);
		}
		args.push("-dce");
		args.push("full");
		for (a in ["-D", "no-inline", "--no-output", "-D", LISTING, "--json", list])
			args.push(a);

		final p = new Process(compiler, args);
		final out = p.stdout.readAll().toString();
		final err = p.stderr.readAll().toString();
		final code = p.exitCode();
		p.close();
		if (code != 0 || !FileSystem.exists(list))
			return fail('WebHost: the listing compile failed (exit $code)\n  $compiler ${args.join(" ")}\n$err$out');

		final types:Array<Dynamic> = Json.parse(File.getContent(list));
		for (t in types) {
			final pack:Array<String> = t.pack;
			if (t.name != "Raw" || pack == null || pack.join(".") != "wgr.impl")
				continue;
			final statics:Array<Dynamic> = t.args.statics;
			final names = [for (s in statics) if ((s.name : String).startsWith("wgr_")) (s.name : String)];
			if (names.length == 0)
				return fail('WebHost: the listing found wgr.impl.Raw but no wgr_* calls in it; '
					+ 'a guest always makes some, so the listing is wrong. -D wgr-host=full works around it.');
			return dedupe(names);
		}
		return fail('WebHost: wgr.impl.Raw is not in the listing at all. Is this guest built with '
			+ '-lib wgrender-hx? -D wgr-host=full works around it.');
	}

	/**
		The `haxe` running this macro, so the listing compile uses the same compiler.

		Macros run in eval, which is the compiler process, and libuv's exepath returns that
		process's binary on every platform libuv supports. The version check turns any
		surprise into an error instead of a list from a different compiler.
	**/
	static function compilerPath():String {
		final path = switch eval.luv.Path.exePath() {
			case Ok(p): p.toString();
			case Error(e): fail('WebHost: cannot find the running haxe ($e)');
		}
		final p = new Process(path, ["--version"]);
		final version = p.stdout.readAll().toString().trim();
		p.close();
		final running = Context.definedValue("haxe");
		if (version != running)
			fail('WebHost: $path reports $version, but the compiler running this build is $running');
		return path;
	}

	/** This binding's root, from where this file is, whether via haxelib or a -cp checkout. **/
	static function bindingRoot():String {
		final self = FileSystem.fullPath(Context.resolvePath("wgr/macros/WebHost.hx"));
		return Path.directory(Path.directory(Path.directory(Path.directory(self))));
	}

	/** Everything the JS binding can call: `-D wgr-host=full`. **/
	static function bindingExports(binding:String):Array<String> {
		// gen_raw_externs.py writes the list as data beside Raw.js.hx, rather than this
		// reading the generated Haxe as text
		final listed:{functions:Array<String>} = haxe.Json.parse(File.getContent(Path.join([binding, "src/wgr/impl/exports.json"])));
		return dedupe(listed.functions);
	}

	/**
		The web build's settings, as wgrender's web build spells them: `WEB_THREADS`
		(default 0 here: a threaded page needs COOP/COEP headers), `BACKEND`, `WEB_DEBUG`.
	**/
	static function webSettings():Map<String, String>
		return ["BACKEND" => env("BACKEND", "webgl2"), "WEB_THREADS" => env("WEB_THREADS", "0"),
			"WEB_DEBUG" => env("WEB_DEBUG", "0")];

	/**
		wgrender's name for a web build's variant, as its wasm32 presets and build.json
		name it: release, release-threads, release-webgpu, debug, ...
	**/
	static function webVariant(web:Map<String, String>):String
		return (web["WEB_DEBUG"] == "1" ? "debug" : "release") + (web["BACKEND"] == "webgl2" ? "" : '-${web["BACKEND"]}')
			+ (web["WEB_THREADS"] == "1" ? "-threads" : "");

	/**
		The library and the flags a program compiles and links against it with, from
		wgrender's build.json: its build as data.
	**/
	static function webFlags(wgrender:String, web:Map<String, String>):{lib:String, cflags:Array<String>, ldflags:Array<String>} {
		final dir = webVariant(web);
		final manifest = Path.join([wgrender, "build.json"]);
		if (!FileSystem.exists(manifest))
			fail('WebHost: no $manifest.');
		final target:Dynamic = Reflect.field(Reflect.field(haxe.Json.parse(File.getContent(manifest)), "web"), dir);
		if (target == null)
			fail('WebHost: $manifest has no web target $dir');
		// what build_web_library.py (and the wasm32 preset of the same name) makes
		return {lib: 'out/wasm32/$dir/lib/libwgrender.a', cflags: target.program_cflags, ldflags: target.ldflags};
	}

	/**
		The Python to run wgrender's build script on: emsdk's own when it says so
		(`EMSDK_PYTHON`, which emsdk sets on Windows), else the first on the path.
	**/
	static function python():String {
		final emsdk = Sys.getEnv("EMSDK_PYTHON");
		if (emsdk != null && emsdk != "" && FileSystem.exists(emsdk))
			return emsdk;
		for (name in ["python3", "python"])
			try {
				final p = new Process(name, ["--version"]);
				final ok = p.exitCode() == 0;
				p.close();
				if (ok)
					return name;
			} catch (_:Dynamic) {}
		return fail("WebHost: no Python on the path (emsdk brings one: activate emsdk, or set EMSDK_PYTHON)");
	}

	/**
		Which wgrender: the repository this binding lives in (bindings/haxe), as
		`project/Build.xml` finds it for a native build, so a guest's web host and its
		desktop binary always come from the same one.
	**/
	static function findWgrender(binding:String):String {
		final dir = Path.join([binding, "../.."]);
		if (!FileSystem.exists(Path.join([dir, "include/wgr.h"])))
			return fail('WebHost: no wgrender at $dir (no include/wgr.h): the binding is meant to stay in wgrender-c, at bindings/haxe');
		return FileSystem.fullPath(dir);
	}

	static function env(name:String, fallback:String):String {
		final v = Sys.getEnv(name);
		return v != null && v != "" ? v : fallback;
	}

	static function define(name:String, fallback:String):String {
		final v = Context.definedValue(name);
		return v != null && v != "" && v != "1" ? v : fallback;
	}

	static function run(cmd:String, args:Array<String>) {
		// The export list alone is hundreds of names; show the shape, not the payload.
		final shown = [for (a in args) a.length > 120 ? a.substr(0, 60) + '…(${a.length} chars)' : a];
		Sys.println('+ $cmd ${shown.join(" ")}');
		if (Sys.command(cmd, args) != 0)
			fail('WebHost: $cmd failed');
	}

	static function capture(cmd:String, args:Array<String>):String {
		final p = new Process(cmd, args);
		final out = p.stdout.readAll().toString();
		final err = p.stderr.readAll().toString();
		final code = p.exitCode();
		p.close();
		if (code != 0)
			fail('WebHost: $cmd ${args.join(" ")} failed:\n$err$out');
		return out;
	}

	static function dedupe(names:Array<String>):Array<String> {
		final seen = [for (n in names) n => true];
		final out = [for (n in seen.keys()) n];
		out.sort(Reflect.compare);
		return out;
	}

	static function mtime(path:String):String
		return Std.string(FileSystem.stat(path).mtime.getTime());

	static function writeIfChanged(path:String, content:String) {
		if (!FileSystem.exists(path) || File.getContent(path) != content)
			File.saveContent(path, content);
	}

	static function fail<T>(message:String):T {
		Context.fatalError(message, Context.currentPos());
		throw message;
	}

	static function indexHtml(name:String, title:String, background:String):String
		return '<!doctype html>
<!-- Written by wgr.macros.WebHost once; it is yours now, and it will not be overwritten. -->
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>$title</title>
<!-- Fetch the host, the guest and the wasm in parallel rather than one after another. -->
<link rel="modulepreload" href="./boot.js">
<link rel="modulepreload" href="./wgrender-host.js">
<link rel="modulepreload" href="./$name.js">
<link rel="preload" href="./wgrender-host.wasm" as="fetch" type="application/wasm" crossorigin>
<style>
  html, body { margin: 0; height: 100%; background: $background; overflow: hidden; }
  /* CSS sizes the canvas; sokol sizes the drawing buffer from it. */
  /* dvh is the visible height, without the browser bars on a phone; vh, before it, is the fallback for browsers without dvh */
  #canvas { position: fixed; inset: 0; width: 100vw; height: 100vh; height: 100dvh; display: block; outline: none; }
</style>
</head>
<body>
<canvas id="canvas" tabindex="-1" oncontextmenu="event.preventDefault()"></canvas>
<script type="module" src="./boot.js"></script>
</body>
</html>
';

	static function bootJsViaJs(name:String):String
		return '// Generated by wgr.macros.WebHost (-D wgr-js-binding): the calls go through the JS binding.
import createWgrHost from "./wgrender-host.js";
import * as WgrJs from "./wgrender.js";
import { attach } from "./src/runtime.js";
globalThis.WgrJs = WgrJs; // Raw.js.hx (--via-js) calls it as WgrJs
import "./$name.js"; // Haxe output; @:expose puts WgrGuest on the global

const canvas = document.getElementById("canvas");
const host = await createWgrHost({ canvas, print: (t) => console.log(t), printErr: (t) => console.log(t) });
attach(host);
globalThis.WgrGuest.start(host);
';

	static function bootJs(name:String):String
		return '// Generated by wgr.macros.WebHost on every build: it has to match the host.
// Load the host, hand it to the Haxe guest, and let the guest start it.
import createWgrHost from "./wgrender-host.js";
import "./$name.js"; // Haxe output; @:expose puts WgrGuest on the global

const canvas = document.getElementById("canvas");
const host = await createWgrHost({ canvas, print: (t) => console.log(t), printErr: (t) => console.log(t) });
globalThis.WgrGuest.start(host);
';
	#end
}
