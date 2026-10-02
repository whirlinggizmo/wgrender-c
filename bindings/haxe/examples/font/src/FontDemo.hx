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
// and `Text.getDefaultFont()` is the setting that connects them. A font loads on
// create; until it's `Ready` text in it would be in the built-in font, so this example
// shows each line only once its own font has loaded (`font.getStatus()`).
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
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {}); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "font (wgrender host, Haxe guest)", Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(248, 248, 250, 255);
		mono = new Font(MONO_PATH);
		komika = new Font(KOMIKA_PATH);
	}

	static function drawTitle():Void {
		if (komika.getStatus() != Ready)
			return;
		final screen = Window.getScreenSize();
		final size = komika.measure(TITLE, TITLE_SIZE);
		komika.draw(TITLE, (screen.x - size.x) * 0.5, 90.0, TITLE_SIZE, Color.DARKBLUE);
	}

	static function drawSamples():Void {
		if (mono.getStatus() != Ready) {
			Text.draw("loading fonts...", 40, 200, 20, Color.GRAY);
			return;
		}
		mono.draw("The quick brown fox jumps over the lazy dog.", 40, 200, 28, Color.BLACK);
		mono.draw("scalable, anti-aliased TrueType glyphs", 40, 250, 20, Color.DARKGRAY);
		mono.draw("0123456789  !@#$%^&*()  +-*/=", 40, 290, 24, Color.MAROON);
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		if (keys.isPressed(D))
			Text.setDefaultFont(Text.getDefaultFont().isNone() ? komika : Handle.NONE);

		Render.beginFrame();
		Render.clearBackground(background);
		drawTitle();
		drawSamples();
		Text.draw(Text.getDefaultFont().isNone() ? "[D] default font: built in   {a|b} ~ \\ ^_`"
			: "[D] default font: Komika   {a|b} ~ \\ ^_`", 40, 360, 16, Color.DARKGREEN);
		Text.drawFps(12, 12);
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();
	}
}
