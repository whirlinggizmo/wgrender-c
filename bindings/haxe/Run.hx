/**
	`haxelib run wgrender-hx <command>`.

	An installed wgrender-hx carries wgrender as a submodule under project/lib, and
	`haxelib git` fetches it along with everything else, so for a native target there
	is nothing for this to do: hxcpp compiles wgrender's sources with the same
	toolchain it compiles your program with, which is what lets Windows work at all.
	Verified by installing into an empty haxelib repository and building an example
	without running setup at all.

	The web target is different. There wgrender is a wasm *host* the page loads and
	the game is a JS guest on top of it, so the host is an emcc link of wgrender's web
	library, which its tools/buildweb.py builds with emcc and Python alone.

	    haxelib run wgrender-hx setup          fetch or update the wgrender submodule
	    haxelib run wgrender-hx setup web      also build the Emscripten host library
	    haxelib run wgrender-hx where          print what is present

	Normally none of this is needed. `haxelib git` clones submodules, so wgrender is
	already there, and a native build compiles its sources rather than linking a
	library. `setup` is for the cases where that did not happen: a submodule that did
	not come down, or an archive install once this is published, since a haxelib zip
	is flat and carries no submodule.

	Nothing here is needed in a checkout of this repo: its builds pass -D WGRENDER_DIR
	to point at whatever wgrender they are working against.
**/
class Run {
	static var root:String;

	static function main():Void {
		// haxelib runs this with the library directory as the working directory and
		// the caller's directory as the last argument.
		final args = Sys.args();
		root = Sys.getCwd();
		if (args.length > 0 && sys.FileSystem.isDirectory(args[args.length - 1]))
			args.pop();

		switch (args[0]) {
			case "setup":
				setup(args[1] == "web");
			case "where":
				where();
			default:
				Sys.println(help());
				Sys.exit(args[0] == null ? 0 : 1);
		}
	}

	static function help():String
		return "haxelib run wgrender-hx setup [web]   fetch the submodule if it is missing\n"
			+ "haxelib run wgrender-hx where         what is present\n\n"
			+ "Normally neither is needed: `haxelib git` clones submodules, and a native\n"
			+ "build compiles wgrender's sources rather than linking a library.";

	static function wgrender():String
		return haxe.io.Path.join([root, "project/lib/wgrender-c"]);

	static function setup(web:Bool):Void {
		final dir = wgrender();
		// Unconditionally, not only when it is missing. `haxelib git ... --update`
		// pulls the superproject and leaves submodules where they were, so an
		// installed copy can have this library's latest sources beside a wgrender from
		// whenever it was first cloned -- and the binding then reports itself STALE on
		// the first build, correctly and confusingly. Running this every time costs a
		// no-op when nothing moved.
		if (sys.FileSystem.exists(haxe.io.Path.join([root, ".gitmodules"]))) {
			Sys.println("updating the wgrender submodule");
			shell("git", ["submodule", "update", "--init", "--recursive"], root);
		}
		if (!sys.FileSystem.exists(haxe.io.Path.join([dir, "include/wgr.h"]))) {
			Sys.println('no wgrender under $dir.\n'
				+ "If this is a source checkout rather than a haxelib install, there is\n"
				+ "nothing to set up: point -D WGRENDER_DIR at your own wgrender instead.");
			Sys.exit(1);
		}
		Sys.println('wgrender is here: $dir');
		Sys.println("Nothing to build. hxcpp compiles wgrender's sources with your own\n"
			+ "toolchain when you build, so a native target needs no library from here.");

		if (web) {
			// The web target is the exception. There the game is a JS guest on a
			// wgrender wasm *host*, and that host is an emcc link rather than
			// anything hxcpp does, so wgrender's own tools/buildweb.py builds its
			// library: emcc and Python, which emsdk brings, on any OS.
			Sys.println("\nbuilding wgrender (web, no threads)");
			shell(python(), [haxe.io.Path.join([dir, "tools/buildweb.py"]), "WEB_THREADS=0"], dir);
		}
		where();
	}

	static function where():Void {
		final dir = wgrender();
		final have = sys.FileSystem.exists(haxe.io.Path.join([dir, "include/wgr.h"]));
		Sys.println('wgrender:  $dir');
		Sys.println('  sources  ${have ? "present" : "MISSING -- run setup"}');
		final web = haxe.io.Path.join([dir, "out/web/webgl2-nothreads/libwgrender.a"]);
		Sys.println('  web lib  ${sys.FileSystem.exists(web) ? "built" : "not built (setup web)"}');
	}

	/** emsdk's Python when it says so (EMSDK_PYTHON, set on Windows), else python3 or python. **/
	static function python():String {
		final emsdk = Sys.getEnv("EMSDK_PYTHON");
		if (emsdk != null && emsdk != "" && sys.FileSystem.exists(emsdk))
			return emsdk;
		for (name in ["python3", "python"])
			try {
				final p = new sys.io.Process(name, ["--version"]);
				final ok = p.exitCode() == 0;
				p.close();
				if (ok)
					return name;
			} catch (_:Dynamic) {}
		Sys.println("no Python on the path (emsdk brings one: activate emsdk, or set EMSDK_PYTHON)");
		Sys.exit(1);
		return null;
	}

	static function shell(command:String, args:Array<String>, cwd:String):Void {
		final was = Sys.getCwd();
		Sys.setCwd(cwd);
		final code = Sys.command(command, args);
		Sys.setCwd(was);
		if (code != 0)
			Sys.exit(code);
	}
}
