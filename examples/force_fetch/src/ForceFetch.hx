// wgrender's force_fetch example, as a Haxe guest: both of `ensure`'s overrides at
// once, a per-call source URL and a cache bypass.
//
// A port of examples/force_fetch.c. The asset *key* is a path with nothing behind it,
// so a plain load would only fail; the bytes come from an explicit `fetchUrl`, and
// `ForceFetch` makes it go to the network rather than to whatever is cached. That the
// music plays is the proof the override was honoured, and it is cached under the
// bogus key afterwards.
//
// The URL is the asset base's (wgr.Assets), not the server root's: "/assets/..." would
// be wrong anywhere the site is not at one -- GitHub Pages serves a project under
// /<repo>/, and the examples' site keeps the assets beside the pages, not under them.
// An absolute https://cdn.example/... URL passes through the same way.
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
	/** Where the bytes really are, under the asset base. **/
	static inline final MUSIC_FETCH_PATH = "music/a_hero_is_born.mp3";

	static inline final ASSET_MUSIC = 1;

	static var background:Color;
	static var music:Sound;
	static var musicOn = false;

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
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(18, 20, 28, 255);

		if (Wgr.getPlatform() == "web") {
			// The key cannot resolve, so the bytes can only have come from the URL.
			final url = '${Assets.defaultBase()}/$MUSIC_FETCH_PATH';
			GuestAbi.loadAsset(INVALID_MUSIC_PATH, ASSET_MUSIC, url, ForceFetch);
			Log.info('force_fetch: $INVALID_MUSIC_PATH from $url');
		} else {
			// Desktop has no fetcher by default, so both overrides are no-ops there;
			// load the real file from the local asset directory and still play.
			GuestAbi.loadAsset(MUSIC_PATH, ASSET_MUSIC);
			Log.info('force_fetch is web-only; loading $MUSIC_PATH locally on desktop');
		}
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
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
		Text.draw("[M] toggle music   [ESC] quit", 24, 150, 16, Color.LIGHTGRAY);
		Text.drawFps(24, 12);
		Render.endFrame();
	}
}
