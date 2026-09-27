// wgrender's fetch example, as a Haxe guest: the desktop build downloads what the
// browser downloads.
//
// A port of examples/fetch.c. On the web the browser fetches a missing asset and
// caches it. On desktop wgrender ships no HTTP client and no TLS, so it asks the
// program for one: set a URL as the asset host, hand it a fetcher, and a cache miss
// becomes a download.
//
//     Asset.setCacheDir("build/asset-cache");
//     Asset.setHost("https://.../examples/assets");
//     Asset.setFetcher((request, url, destPath) -> ...);
//
// The C example writes a fetcher that shells out to curl, because C has no HTTP in the
// box and the point is to link nothing. Haxe has one, so there is nothing here to
// write: this example's build passes `-D WGR_INCLUDE_FETCHER`, and the binding
// installs `haxe.Http` over hxcpp's bundled mbedtls the first time a URL host is set.
// The program only says where the assets live.
//
// It is a define rather than the default because it costs about a megabyte and almost
// no desktop program needs it: wgrender consults a fetcher only when the host is a URL
// or a task was handed a fetchUrl, and a shipped game's host is the `assets` directory
// beside the executable. Measured: installing it unconditionally grew a guest that
// never downloads anything from 2,807,448 to 4,588,784 bytes. Without the define
// nothing is referenced, so `-dce full` leaves the whole TLS stack out.
//
// Set a URL host in a build without the define and the binding says so once, because
// wgrender's miss path would otherwise just fail the asset without mentioning the one
// thing that was missing.
//
// Bytes never cross the boundary. wgrender names a URL and a destination file and the
// fetcher writes that file, which is what curl, WinHTTP and NSURLSession all hand you
// anyway. Downloads land in the cache directory and the next run finds them there,
// which is the job the browser's cache does on the web.
//
// The guards here are Haxe's, not wgrender's, and that distinction is the point.
// `Asset.setFetcher` compiles on both targets and answers false on the web, where
// there is nothing to install -- the browser is the downloader. What needs `#if sys`
// is reading an environment variable and writing a file, which are facts about the
// standard library rather than about the binding.
//
//   ESC  quit
import wgr.*;

@:expose("WgrGuest")
class Fetch {
	static inline final SCREEN_WIDTH = 1024;
	static inline final SCREEN_HEIGHT = 640;
	static inline final TEXTURE_PATH = "sprites/logo/wg-logo-white-alpha.png";
	static inline final ASSET_TEXTURE = 1;
	// the build's work directory, build/<os>/<variant> (wgr.macros.NativeOut); on the
	// web the browser caches
	static final CACHE_DIR = (haxe.macro.Compiler.getDefine("wgr-work-dir") ?? "build") + "/asset-cache";
	static inline final DEFAULT_HOST =
		"https://raw.githubusercontent.com/whirlinggizmo/wgrender-c/main/examples/assets";

	static var background:Color;
	static var sprite:Sprite2D;
	static var camera:Camera3D;
	static var host = "";
	static var remote = false;
	static var downloads = 0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(hostModule:Dynamic):Bool {
		GuestAbi.attach(hostModule);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "fetch (wgrender host, Haxe guest)", Resizable);
	}

	static function onInit():Void {
		background = Color.rgba(28, 30, 38, 255);
		camera = new Camera3D(Perspective);
		sprite = new Sprite2D(Handle.NONE); // the texture is attached when it loads
		sprite.setPosition(512, 380);
		Debug.enableFps(12, 10, 16);

		#if sys
		// Native: pick a host, check something is serving there, and install a
		// downloader. Sys.getEnv and Sys.command are what need the guard -- they are
		// Haxe's, not wgrender's -- while Asset.setFetcher compiles either way.
		final wanted = Sys.getEnv("WGRENDER_ASSET_HOST");
		host = wanted != null ? wanted : DEFAULT_HOST;
		remote = hostIsUp(host);
		if (remote) {
			Asset.setCacheDir(CACHE_DIR);
		} else {
			host = Assets.defaultBase(); // the local directory
		}
		#else
		host = Assets.defaultBase(); // the browser fetches
		remote = true;
		#end
		Asset.setHost(host);

		if (!GuestAbi.loadAsset(TEXTURE_PATH, ASSET_TEXTURE))
			Log.error('failed to queue asset: $TEXTURE_PATH');
	}

	#if sys
	/** Is anything serving there? Keeps an offline run, and a forgetful human, honest. **/
	static function hostIsUp(host:String):Bool {
		var up = false;
		final http = new haxe.Http('$host/$TEXTURE_PATH');
		http.cnxTimeout = 2; // per request, so an offline run gives up quickly
		http.onStatus = status -> up = status >= 200 && status < 400;
		http.onError = _ -> up = false;
		try
			http.request(false)
		catch (_:Dynamic)
			up = false;
		return up;
	}
	#end

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('could not get $path');
			return;
		}
		if (id != ASSET_TEXTURE)
			return;
		final texture = new Texture(path);
		sprite.setTexture(texture);
		texture.release(); // the sprite holds its own reference
	}

	static function onFrame(dt:Float):Void {
		Render.beginFrame();
		Render.clearBackground(background);
		sprite.draw();
		Text.draw("wgrender fetch: the desktop build downloads what the browser downloads", 12, 36, 20,
			Color.RAYWHITE);
		Text.draw('host: $host', 12, 64, 16, Color.LIGHTGRAY);
		#if sys
		Text.draw(remote ? 'downloaded $downloads file(s) into $CACHE_DIR   (delete it and re-run: they come back)'
			: 'no host reachable — reading ${Assets.defaultBase()} locally instead', 12, 86, 16, Color.LIGHTGRAY);
		#end
		Render.endFrame();

		if (Input.getKeyboardState().isPressed(Escape))
			Wgr.requestQuit();
	}
}
