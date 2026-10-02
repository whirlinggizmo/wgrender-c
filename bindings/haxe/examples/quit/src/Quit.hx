// wgrender's quit example, as a Haxe guest: requesting a quit while audio is playing
// and files are still in flight.
//
// A port of examples/quit.c. It plays music, and after a second starts two
// environment loads it never waits for, stalls the frame for half a second the way a
// long synchronous load would, and quits. That is the path that used to crash on the
// web, where audio events queued during the busy frame ran after shutdown -- so the
// point of the example is that it survives, not that it does anything.
//
// It is also the example that made the binding expose the guest ABI's fourth op:
// `wgr_guest.h` has had a shutdown slot all along and `GuestAbi.register` was passing
// 0 for it, because nothing had needed wgrender's `wgr_set_shutdown` yet.
//
//   Q    quit now
//   ESC  quit now (desktop)
import wgr.*;

@:expose("WgrGuest")
class Quit {
	static inline final SCREEN_WIDTH = 640;
	static inline final SCREEN_HEIGHT = 200;
	static inline final MUSIC_PATH = "music/a_hero_is_born.mp3";
	static inline final ENV_A_PATH = "environments/venice_sunset_1k.hdr";
	static inline final ENV_B_PATH = "environments/studio_small_09_1k.hdr";

	static inline final ASSET_BGM = 1;
	// The two environments are only ever started, so that something is in flight when
	// the quit lands. Their ids exist to tell the failures apart in the log.
	static inline final ASSET_ENV_A = 2;
	static inline final ASSET_ENV_B = 3;

	static inline final STALL = 0.5;

	static var background:Color;
	static var music:Sound;
	static var quitAt = 0.0;
	static var quitting = false;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset, onShutdown);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "quit (wgrender host, Haxe guest)", Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(30, 36, 48, 255);
		// Soon enough that a headless check sees the quit happen.
		quitAt = Wgr.getTime() + 1.0;
		if (!GuestAbi.loadAsset(MUSIC_PATH, ASSET_BGM))
			Log.error('failed to queue asset: $MUSIC_PATH');
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			// Expected for the environments: the quit lands before they arrive.
			Log.info('asset did not complete: $path');
			return;
		}
		if (id == ASSET_BGM) {
			final audio = new Audio(path);
			music = new Sound(audio);
			audio.release(); // the sound keeps its own reference
			music.setLoop(true);
			music.play();
		}
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		if (keys.isPressed(Q))
			Wgr.requestQuit();
		// on the web Escape is the browser's (it leaves fullscreen): Q quits there
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();

		Render.beginFrame();
		Render.clearBackground(background);
		Text.draw("wgrender quit (Haxe guest)   Q: quit now", 12, 12, 16, Color.RAYWHITE);
		if (quitting)
			Text.draw("quit requested: shutdown runs after this frame", 12, 40, 16, Color.LIGHTGRAY);
		else
			Text.draw('loading, stalling and quitting in ${fixed(quitAt - Wgr.getTime(), 1)} s', 12, 40, 16,
				Color.LIGHTGRAY);
		Render.endFrame();

		if (!quitting && Wgr.getTime() >= quitAt) {
			quitting = true;
			GuestAbi.loadAsset(ENV_A_PATH, ASSET_ENV_A);
			GuestAbi.loadAsset(ENV_B_PATH, ASSET_ENV_B);
			// A deliberately long synchronous frame, with those two loads outstanding.
			final busyUntil = Wgr.getTime() + STALL;
			while (Wgr.getTime() < busyUntil) {}
			Wgr.requestQuit();
		}
	}

	/** wgrender's `wgr_set_shutdown`, reached through the guest ABI's shutdown op. **/
	static function onShutdown():Void {
		Log.info("quit: shutdown");
	}

	/** The same by-hand formatter the other examples carry; Haxe has no printf. **/
	static function fixed(value:Float, decimals:Int):String {
		final negative = value < 0;
		var digits = Std.string(Math.round(Math.abs(value) * Math.pow(10, decimals)));
		while (digits.length <= decimals)
			digits = "0" + digits;
		final point = digits.length - decimals;
		return (negative ? "-" : "") + digits.substr(0, point) + (decimals > 0 ? "." + digits.substr(point) : "");
	}
}
