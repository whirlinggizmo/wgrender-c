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
// Two buttons (`UiWidgets.hx`, from examples/shared/ui) show the cache at work: Fetch
// asset loads the logo again (releasing the texture, then creating it), and Clear
// cache forgets what was downloaded, so the next fetch downloads it again. The line
// under them says what happened -- natively, whether
// the file came from the cache or was downloaded, told by looking in the cache
// directory first (`Asset.getCacheDir`). The web keeps its cache in the browser, where
// the example can't look, so there it just says loaded.
//
//   ESC  quit
import UiWidgets;
import wgr.*;

@:expose("WgrGuest")
class Fetch {
	static inline final SCREEN_WIDTH = 1024;
	static inline final SCREEN_HEIGHT = 640;
	static inline final TEXTURE_PATH = "sprites/logo/wg-logo-white-alpha.png";
	static inline final LAYER_CONTROL = 1; // a button's label goes on the layer above
	// the build's work directory, build/<preset> (wgr.macros.NativeOut); on the
	// web the browser caches
	static final CACHE_DIR = (haxe.macro.Compiler.getDefine("wgr-work-dir") ?? "build") + "/asset-cache";
	static inline final DEFAULT_HOST =
		"https://raw.githubusercontent.com/whirlinggizmo/wgrender-c/main/examples/assets";

	static var background:Color;
	static var sprite:Sprite2D;
	static var texture:Texture; // the logo; watched until it's Ready or Failed
	static var waiting = false;
	static var camera:Camera3D;
	static var host = "";
	static var remote = false;
	static var scene:Scene;
	static var theme:UiTheme;
	static var fetchButton:UiButton;
	static var clearButton:UiButton;
	static var state = "";
	static var wasCached = false; // the file was in the cache before this fetch

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(hostModule:Dynamic):Bool {
		GuestAbi.attach(hostModule);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {}); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "fetch (wgrender host, Haxe guest)", Resizable);
	}

	static function onInit():Void {
		background = Color.rgba(28, 30, 38, 255);
		camera = new Camera3D(Perspective);
		texture = Handle.NONE;
		sprite = new Sprite2D(Handle.NONE); // the texture is attached by fetch
		sprite.setPosition(512, 380);
		Debug.enableFps(12, 10, 16);

		theme = new UiTheme();
		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setInteractive(true);
		fetchButton = new UiButton(scene, LAYER_CONTROL, "Fetch asset", 12, 150, 180, 40, 17);
		clearButton = new UiButton(scene, LAYER_CONTROL, "Clear cache", 204, 150, 180, 40, 17);

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
		fetch();
	}

	/** Load the logo again, having noted whether the cache has it already. **/
	static function fetch():Void {
		wasCached = false;
		#if sys
		if (remote)
			wasCached = sys.FileSystem.exists('${Asset.getCacheDir()}/$TEXTURE_PATH');
		#end
		// nothing may hold the old texture, or creating the path again finds it loaded
		sprite.setTexture(Handle.NONE);
		texture.release();
		texture = new Texture(TEXTURE_PATH); // Pending: made local (from the cache, or downloaded), then loaded
		sprite.setTexture(texture); // drawn once it's Ready
		waiting = true;
		fetchButton.enabled = false;
		state = 'fetching $TEXTURE_PATH...';
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

	/** The logo finished loading: say where it came from. **/
	static function reportLoaded():Void {
		#if sys
		state = !remote ? 'read $TEXTURE_PATH from ${Assets.defaultBase()}'
			: wasCached ? 'loaded $TEXTURE_PATH from the cache' : 'downloaded $TEXTURE_PATH into the cache';
		#else
		state = 'loaded $TEXTURE_PATH';
		#end
	}

	static function onFrame(dt:Float):Void {
		if (waiting && texture.getStatus() != Pending) {
			waiting = false;
			fetchButton.enabled = true;
			if (texture.getStatus() == Ready)
				reportLoaded();
			else
				state = 'failed to get $TEXTURE_PATH'; // the log says why
		}
		if (fetchButton.update(scene, theme))
			fetch();
		if (clearButton.update(scene, theme)) {
			Asset.clearCache();
			state = "cache cleared: the next fetch downloads";
		}

		Render.beginFrame();
		Render.clearBackground(background);
		sprite.draw();
		scene.draw();
		Text.draw("wgrender fetch: the desktop build downloads what the browser downloads", 12, 36, 20,
			Color.RAYWHITE);
		Text.draw('host: $host', 12, 64, 16, Color.LIGHTGRAY);
		#if sys
		Text.draw(remote ? 'cache: ${Asset.getCacheDir()}'
			: 'no host reachable — reading ${Assets.defaultBase()} locally instead', 12, 86, 16, Color.LIGHTGRAY);
		#end
		Text.draw(state, 12, 120, 18, Color.SKYBLUE);
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.getKeyboardState().isPressed(Escape))
			Wgr.requestQuit();
	}
}
