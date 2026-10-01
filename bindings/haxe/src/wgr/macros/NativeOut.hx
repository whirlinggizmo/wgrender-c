package wgr.macros;

#if macro
import haxe.io.Path;
import haxe.macro.Compiler;
import haxe.macro.Context;
import sys.FileSystem;
#end

/**
	Where an hxcpp build's C++ goes, for an hxml that is the same on every OS:

	```
	--cpp build/cpp
	--macro wgr.macros.NativeOut.build()
	```

	moves hxcpp's output to `build/<platform>/<variant>/cpp`, the wg* family layout
	(whirlinggizmo/.github CONVENTIONS.md, "Build directories"): `build/linux/release/cpp`,
	`build/macos/release/cpp`, and on Windows `build/windows/msvc/cpp` or, with MinGW
	(`-D mingw` or `HXCPP_MINGW`), `build/windows/mingw/cpp`. hxcpp's emscripten target
	(`-D emscripten`) is `build/web/webgl2-nothreads/cpp` (`webgpu` with `-D wgr-webgpu`,
	`-debug` with `--debug`). A committed hxml can't name the OS it's built on; this
	does it at compile time. Optional: leave the line out and `--cpp` is where the
	output goes.

	It also defines `wgr-work-dir` as that build's work directory (`build/linux/release`),
	for a program that keeps files of its own there, such as a download cache:
	`haxe.macro.Compiler.getDefine("wgr-work-dir")`.

	And it does what `toolchain()` does.
**/
class NativeOut {
	#if macro
	public static function build() {
		if (!Context.defined("cpp"))
			return;
		final dir = if (Context.defined("emscripten")) {
			'web/' + (Context.defined("wgr-webgpu") ? "webgpu" : "webgl2") + "-nothreads" + (Context.defined("debug") ? "-debug" : "");
		} else {
			final os = switch (Sys.systemName()) {
				case "Windows": "windows";
				case "Mac": "macos";
				case other: other.toLowerCase();
			}
			'$os/' + (os != "windows" ? "release" : (Context.defined("mingw") || Sys.getEnv("HXCPP_MINGW") != null) ? "mingw" : "msvc");
		}
		Compiler.define("wgr-work-dir", 'build/$dir');
		Compiler.setOutput('build/$dir/cpp');
		toolchain();
	}

	/**
		hxcpp's emscripten target on Windows runs `python "${EMSCRIPTEN_SDK}/emcc.py"`,
		and neither is found for it: this names the directory of the `emcc` on the path
		(`EMSCRIPTEN_SDK`) and emsdk's own Python (`EMSCRIPTEN_PYTHON`, from the
		`EMSDK_PYTHON` emsdk sets), unless the build already defines them. Elsewhere, and
		for native builds, hxcpp runs `emcc` from the path and needs nothing.
	**/
	public static function toolchain() {
		if (!Context.defined("emscripten") || Sys.systemName() != "Windows")
			return;
		if (!Context.defined("EMSCRIPTEN_SDK")) {
			final found = [for (d in (Sys.getEnv("PATH") ?? "").split(";")) if (d != "" && FileSystem.exists(Path.join([d, "emcc.py"]))) d];
			if (found.length == 0)
				Context.fatalError("hxcpp's emscripten build needs emcc on the path (emsdk_env), or -D EMSCRIPTEN_SDK=<dir of emcc.py>",
					Context.currentPos());
			Compiler.define("EMSCRIPTEN_SDK", found[0]);
		}
		final python = Sys.getEnv("EMSDK_PYTHON");
		if (!Context.defined("EMSCRIPTEN_PYTHON") && python != null && python != "")
			Compiler.define("EMSCRIPTEN_PYTHON", python);
	}
	#end
}
