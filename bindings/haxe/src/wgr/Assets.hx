package wgr;

/**
	Where assets load from — the base `Asset.setHost` wants, resolved at run time.

	Deliberately no compile-time define: baking the build machine's path into the
	binary makes it stop working the moment the asset tree moves, and makes a
	development build resolve assets differently from a shipped one. Here they resolve
	the same way, so what you run is what you ship.
**/
class Assets {
	/** The environment variable that overrides everything else. **/
	public static inline final OVERRIDE = "WGR_ASSET_BASE";

	/** What a program looks for beside itself. **/
	public static inline final BESIDE = "assets";

	/**
		The manifest under the asset base, for `Asset.setManifest`: the published site's
		assets have one (examples/build.py site writes it with tools/gen_manifest.py),
		so a returning visitor fetches only what changed. Where there is none, as under
		tools/serve.py, the cache asks the host about each file instead; natively the
		base is a directory and it's ignored.
	**/
	public static inline final MANIFEST = "manifest.json";

	/**
		On the web — either web build — `assets` beside the page, relative, as wgrender's
		own web examples have it, so a site works at a domain root or under a path
		(GitHub Pages serves a project at /<repo>/). tools/serve.py mounts wgrender's
		asset tree at `/assets`, which is beside a page served at the root. A page
		elsewhere says where with `<meta name="wgr-asset-base" content="../assets">`
		(examples/build.py site gives each example's page one, beside the shared tree).

		Natively, in order:

		1. `$WGR_ASSET_BASE`, so a run can be pointed anywhere without rebuilding;
		2. an `assets` directory beside the executable, which is what a built program
		   has — a development build gets one as a link to wherever wgrender is;
		3. `assets` relative to the working directory.
	**/
	public static function defaultBase():String {
		// Both web builds — Haxe to JS, and hxcpp through Emscripten — are served the
		// same way. Asking `sys` here instead would drag sys.FileSystem and
		// Sys.programPath into the wasm to answer a question with one answer (+50 KB,
		// measured).
		#if js
		final meta:String = js.Syntax.code("(document.querySelector('meta[name=\"wgr-asset-base\"]') || {}).content");
		return meta != null && meta != "" ? meta : "assets";
		#elseif emscripten
		return "assets";
		#elseif sys
		final fromEnv = Sys.getEnv(OVERRIDE);
		if (fromEnv != null && fromEnv != "")
			return fromEnv;
		final beside = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), BESIDE]);
		if (sys.FileSystem.exists(beside))
			return beside;
		return BESIDE;
		#else
		return BESIDE;
		#end
	}
}
