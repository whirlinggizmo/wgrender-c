package wgr.macros;

#if macro
import haxe.Json;
import haxe.macro.Compiler;
import haxe.macro.Context;
import haxe.macro.Expr;
import haxe.macro.ExprTools;
import haxe.macro.PositionTools;
import sys.io.File;
#end

/**
	The binding's functions as the Haxe parser reads them, for tools/members.py: every
	function in `src/wgr` with its name as written, whether it's public, its doc comment,
	the C calls its body makes through `Raw` or `GuestRaw`, and the functions of its own
	type it calls (an overload makes its C call through a private `...Raw` member).
	Written as JSON to the file given, once typing is done:

	```
	haxe -cp src --macro "wgr.macros.Members.dump('members.json')" --macro "include('wgr', true, ['wgr.macros'])" -js x.js --no-output
	```

	A build macro on every type in the package, so a body is the expression as written:
	typing would inline what it calls (an abstract's methods are resolved by inlining
	whatever --no-inline says), and a member's own calls are the ones that matter. Per
	target, since some types are a file per target (GuestAbi.cpp.hx, GuestAbi.js.hx);
	members.py runs it for both and merges them.
**/
class Members {
	#if macro
	static var found:Array<Dynamic> = [];

	public static function dump(path:String) {
		Compiler.addGlobalMetadata("wgr", "@:build(wgr.macros.Members.collect())", true, true, false);
		var done = false;
		Context.onAfterTyping(_ -> {
			if (done)
				return;
			done = true;
			File.saveContent(path, Json.stringify(found));
		});
	}

	/**
		The main class the command line names (an hxml's --main), printed, before anything
		is typed: `haxe build.web.hxml --no-output --macro wgr.macros.Members.mainClass()`.
	**/
	public static function mainClass() {
		final c = Compiler.getConfiguration().mainClass;
		Sys.println(c == null ? "" : c.pack.concat([c.name]).join("."));
		Sys.exit(0);
	}

	/** WebHost's two hand-kept lists, as the compiler has them, printed as JSON. **/
	public static function lists() {
		Sys.println(Json.stringify({abi: WebHost.GUEST_ABI, runtime: WebHost.RUNTIME_METHODS}));
	}

	public static function collect():Array<Field> {
		final local = Context.getLocalModule().split(".");
		final fields = Context.getBuildFields();
		if (local[0] != "wgr" || local[1] == "macros")
			return null;
		for (f in fields)
			switch (f.kind) {
				case FFun(fn):
					final calls = [], own = [], host = [];
					if (fn.expr != null)
						walk(fn.expr, calls, own, host);
					final at = PositionTools.toLocation(f.pos);
					found.push({
						module: local[local.length - 1],
						// wgr.impl: Raw, GuestRaw and the rest the layer is built on, not the layer
						impl: local[1] == "impl",
						// the parser names an abstract's constructor _new; the source says new
						name: f.name == "_new" ? "new" : f.name,
						"public": f.access != null && f.access.contains(APublic),
						doc: f.doc == null ? "" : f.doc,
						calls: calls,
						local: own,
						// what a JS guest reaches on the Emscripten module (`host.HEAPU8`)
						host: host,
						file: haxe.io.Path.withoutDirectory(at.file.toString()),
						line: at.range.start.line
					});
				default:
			}
		return null;
	}

	static function walk(e:Expr, calls:Array<String>, own:Array<String>, host:Array<String>) {
		switch (e.expr) {
			case EField({expr: EConst(CIdent("Raw" | "GuestRaw"))}, name) if (!calls.contains(name)):
				calls.push(name);
			case EField({expr: EConst(CIdent("host")) | EField({expr: EConst(CIdent("Raw"))}, "host")}, name) if (!host.contains(name)):
				host.push(name);
			case ECall({expr: EConst(CIdent(name))}, _) if (!own.contains(name)):
				own.push(name);
			default:
		}
		ExprTools.iter(e, x -> walk(x, calls, own, host));
	}
	#end
}
