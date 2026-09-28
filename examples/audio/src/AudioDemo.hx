// wgrender's audio example, as a Haxe guest: looping mp3 music and a one-shot ogg.
//
// A port of examples/audio.c. Each file is made local, then `new Audio(path)`
// makes the shared resource and a `Sound` plays it -- the music (over 1 MB) streamed, the
// small click decoded up front.
//
// S is the interesting key. On desktop the mixer runs on the audio device's thread,
// so a 300 ms frame does not touch the music. On the web it stutters, because
// sokol_audio's WebAudio callback runs on the main thread -- the one the stall
// blocks -- and its ~46 ms buffer runs dry. The stall is the same line of Haxe on
// both, and the difference is entirely where the mixer lives.
//
//   SPACE  play the click
//   M      toggle the music
//   S      stall one frame for 300 ms
//   ESC    quit
//
// The class is `AudioDemo` rather than `Audio` because a module named `Audio` would
// shadow `wgr.Audio` inside itself.
import wgr.*;

@:expose("WgrGuest")
class AudioDemo {
	static inline final SCREEN_WIDTH = 720;
	static inline final SCREEN_HEIGHT = 240;
	static inline final MUSIC_PATH = "music/a_hero_is_born.mp3";
	static inline final CLICK_PATH = "sounds/click_004.ogg";

	static inline final ASSET_MUSIC = 1;
	static inline final ASSET_CLICK = 2;

	static inline final STALL = 0.3;

	static var background:Color;
	static var music:Sound;
	static var click:Sound;
	static var musicOn = false;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "audio (wgrender host, Haxe guest)", Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(18, 20, 28, 255);
		load(MUSIC_PATH, ASSET_MUSIC);
		load(CLICK_PATH, ASSET_CLICK);
	}

	static function load(path:String, id:Int):Void {
		if (!GuestAbi.loadAsset(path, id))
			Log.error('failed to queue asset: $path');
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		final audio = new Audio(path);
		final sound = new Sound(audio);
		audio.release(); // the sound holds its own reference
		switch id {
			case ASSET_MUSIC:
				music = sound;
				music.setVolume(0.5);
				music.setLoop(true); // "music" is just a looping sound
				music.play();
				musicOn = true;

			case ASSET_CLICK:
				click = sound;
				click.setVolume(1.0);
		}
	}

	static function handleKeys():Void {
		final keys = Input.getKeyboardState();

		if (keys.isPressed(Space) && !click.isNone())
			click.play();
		if (keys.isPressed(S)) {
			final until = Wgr.getTime() + STALL; // a deliberately slow frame
			while (Wgr.getTime() < until) {}
		}
		if (keys.isPressed(M) && !music.isNone()) {
			if (musicOn)
				music.pause();
			else
				music.resume();
			musicOn = !musicOn;
		}
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();
	}

	static function onFrame(dt:Float):Void {
		handleKeys();

		Render.beginFrame();
		Render.clearBackground(background);
		Text.draw("wgrender + sokol_audio (Haxe guest)", 24, 30, 28, Color.RAYWHITE);
		Text.draw(music.isNone() ? "music: loading..."
			: (musicOn ? "music: playing (mp3, streamed, looping)" : "music: paused"), 24, 80, 18, Color.SKYBLUE);
		Text.draw(click.isNone() ? "click: loading..." : "click: ready (ogg)", 24, 110, 18, Color.LIME);
		Text.draw("[SPACE] play click   [M] toggle music   [S] stall 300 ms" + (Wgr.getPlatform() == "web" ? "" : "   [ESC] quit"), 24, 150, 16,
			Color.LIGHTGRAY);
		Text.drawFps(24, 12);
		Render.endFrame();
	}
}
