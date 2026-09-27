// wgrender's force_fetch example, as a Haxe guest: both of `ensure`'s overrides at
// once, a per-call source URL and a cache bypass.
//
// A port of examples/force_fetch.c. The asset *key* is a path with nothing behind it,
// so a plain load would only fail; the bytes come from an explicit `fetchUrl`, and
// `ForceFetch` makes it go to the network rather than to whatever is cached. That the
// music plays is the proof the override was honoured, and it is cached under the
// bogus key afterwards.
//
// The source is relative, so it is read against the asset host as a browser reads a
// URL against a directory, and the call is the same everywhere: under the page's
// assets on the web (at a domain root or under GitHub Pages' /<repo>/ alike), under
// the remote host on desktop, which downloads it through the fetcher the build asks
// for with `-D WGR_INCLUDE_FETCHER`, into the cache directory under the bogus key.
// With no network desktop switches to the local asset directory and asks again, and
// the file is read where it is, still under the bogus key. An absolute
// https://cdn.example/... source is used as it is.
//
// This is the example that widened the guest ABI. `wgr_guest_asset_load` took a path
// and an id, which is everything the earlier examples need and nothing this one does,
// so it now takes wgrender's `fetch_url` and `flags` as well.
//
//   M    toggle the music
//   ESC  quit
import wgr.*;

@:expose("WgrGuest")
class ForceFetch {
	static inline final SCREEN_WIDTH = 720;
	static inline final SCREEN_HEIGHT = 240;

	static inline final MUSIC_PATH = "music/a_hero_is_born.mp3";
	/** Deliberately wrong: nothing is served at host + this, so only the override works. **/
	static inline final INVALID_MUSIC_PATH = "music/invalid.mp3";
	/** Where the bytes really are, relative to the asset host. **/
	static inline final MUSIC_SOURCE = "music/a_hero_is_born.mp3";

	static inline final ASSET_MUSIC = 1;

	/** Desktop's host, the same one examples/fetch downloads from. **/
	static inline final REMOTE_HOST =
		"https://raw.githubusercontent.com/whirlinggizmo/wgrender-c/main/examples/assets";
	/** the build's work directory, build/<os>/<variant> (wgr.macros.NativeOut) **/
	static final CACHE_DIR = (haxe.macro.Compiler.getDefine("wgr-work-dir") ?? "build") + "/asset-cache";

	static var background:Color;
	static var music:Sound;
	static var musicOn = false;
	static var offline = false;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "force_fetch (wgrender host, Haxe guest)", Resizable);
	}

	static function onInit():Void {
		Log.setLevel(Info);
		background = Color.rgba(18, 20, 28, 255);

		if (Wgr.getPlatform() == "web") {
			Asset.setHost(Assets.defaultBase());
			Asset.setManifest(Assets.MANIFEST);
		} else {
			// No manifest: the remote host has none, and a task with a source of its
			// own isn't checked against one anyway.
			Asset.setCacheDir(CACHE_DIR);
			Asset.setHost(REMOTE_HOST);
		}
		load();
	}

	/** The key cannot resolve, so the bytes can only have come from the source. **/
	static function load():Void {
		GuestAbi.loadAsset(INVALID_MUSIC_PATH, ASSET_MUSIC, MUSIC_SOURCE, ForceFetch);
		Log.info('force_fetch: $INVALID_MUSIC_PATH from $MUSIC_SOURCE under ${Asset.getHost()}');
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			if (id == ASSET_MUSIC && !offline && Wgr.getPlatform() != "web") {
				// desktop with no network: the same call, against the local directory
				offline = true;
				Asset.setHost(Assets.defaultBase());
				load();
				return;
			}
			Log.error('load failed: $path');
			return;
		}
		if (id != ASSET_MUSIC)
			return;
		final audio = new Audio(path);
		music = new Sound(audio);
		audio.release(); // the sound holds its own reference
		music.setVolume(0.5);
		music.setLoop(true); // "music" is just a looping sound
		music.play();
		musicOn = true;
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();

		if (keys.isPressed(M) && !music.isNone()) {
			if (musicOn)
				music.pause();
			else
				music.resume();
			musicOn = !musicOn;
		}
		if (keys.isPressed(Escape))
			Wgr.requestQuit();

		Render.beginFrame();
		Render.clearBackground(background);
		Text.draw("wgrender + sokol_audio + force_fetch (Haxe guest)", 24, 30, 28, Color.RAYWHITE);
		Text.draw(music.isNone() ? "music: loading..."
			: (musicOn ? "music: playing (mp3, looping)" : "music: paused"), 24, 80, 18, Color.SKYBLUE);
		Text.draw((offline ? "no download; read in place under " : "from ") + '${Asset.getHost()}/$MUSIC_SOURCE', 24,
			110, 14, Color.LIGHTGRAY);
		Text.draw("[M] toggle music   [ESC] quit", 24, 150, 16, Color.LIGHTGRAY);
		Text.drawFps(24, 12);
		Render.endFrame();
	}
}
