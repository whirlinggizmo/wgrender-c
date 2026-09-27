// wgrender's font example, as a Haxe guest: TrueType text through fontstash.
//
// A port of examples/font.c. Two fonts are loaded asynchronously, then drawn at
// several sizes, with the title measured and centred. D swaps what `Text.draw` uses
// when it is given no font.
//
//   D    switch the default font between the built-in one and Komika
//   ESC  quit
//
// `Text` and `Font` split wgrender's two families of text calls: the `wgr_text_draw`
// group, which uses whatever the default font is, and the `wgr_text_draw_ex` group,
// which takes a handle. So `Text.draw` is the first and `font.draw` is the second,
// and `Text.getDefaultFont()` is the setting that connects them. `Font.isNone` is the
// check for "not loaded yet", because a handle is 0 rather than null.
//
// The class is `FontDemo` rather than `Font` because a module named `Font` would
// shadow `wgr.Font` inside itself.
import wgr.*;

@:expose("WgrGuest")
class FontDemo {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 500;
	static inline final MONO_PATH = "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf";
	static inline final KOMIKA_PATH = "fonts/Komika/KOMIKAH_.ttf";

	static inline final ASSET_MONO = 1;
	static inline final ASSET_KOMIKA = 2;

	static inline final TITLE = "wgrender + fontstash";
	static inline final TITLE_SIZE = 56.0;

	static var background:Color;
	static var mono:Font;
	static var komika:Font;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "font (wgrender host, Haxe guest)", Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(248, 248, 250, 255);
		load(MONO_PATH, ASSET_MONO);
		load(KOMIKA_PATH, ASSET_KOMIKA);
	}

	static function load(path:String, id:Int):Void {
		if (!GuestAbi.loadAsset(path, id))
			Log.error('failed to queue asset: $path');
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('font load failed: $path');
			return;
		}
		switch id {
			case ASSET_MONO: mono = new Font(path);
			case ASSET_KOMIKA: komika = new Font(path);
		}
	}

	static function drawTitle():Void {
		if (komika.isNone())
			return;
		final screen = Window.getScreenSize();
		final size = komika.measure(TITLE, TITLE_SIZE);
		komika.draw(TITLE, (screen.x - size.x) * 0.5, 90.0, TITLE_SIZE, Color.DARKBLUE);
	}

	static function drawSamples():Void {
		if (mono.isNone()) {
			Text.draw("loading fonts...", 40, 200, 20, Color.GRAY);
			return;
		}
		mono.draw("The quick brown fox jumps over the lazy dog.", 40, 200, 28, Color.BLACK);
		mono.draw("scalable, anti-aliased TrueType glyphs", 40, 250, 20, Color.DARKGRAY);
		mono.draw("0123456789  !@#$%^&*()  +-*/=", 40, 290, 24, Color.MAROON);
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		if (keys.isPressed(D) && !komika.isNone())
			Text.setDefaultFont(Text.getDefaultFont().isNone() ? komika : Handle.NONE);

		Render.beginFrame();
		Render.clearBackground(background);
		drawTitle();
		drawSamples();
		Text.draw(Text.getDefaultFont().isNone() ? "[D] default font: built in   {a|b} ~ \\ ^_`"
			: "[D] default font: Komika   {a|b} ~ \\ ^_`", 40, 360, 16, Color.DARKGREEN);
		Text.drawFps(12, 12);
		Render.endFrame();

		if (keys.isPressed(Escape))
			Wgr.requestQuit();
	}
}
