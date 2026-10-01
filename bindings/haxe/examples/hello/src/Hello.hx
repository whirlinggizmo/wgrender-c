// wgrender's hello, as a Haxe guest: a window, 2D shapes, text and the mouse.
// A port of examples/hello.c.
import wgr.*;

@:expose("WgrGuest")
class Hello {
	static inline final SCREEN_WIDTH = 800;
	static inline final SCREEN_HEIGHT = 600;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(() -> {}, (dt, _) -> onFrame(dt), (_, _, _) -> {});
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "hello (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onFrame(dt:Float):Void {
		final mouse = Input.getMouseState();

		Render.beginFrame();
		Render.clearBackground(Color.RAYWHITE);

		// filled + outlined rectangles
		Shape2D.drawRectangle(40, 40, 200, 120, Color.SKYBLUE);
		Shape2D.drawRectangleLines(40, 40, 200, 120, Color.DARKBLUE);

		// line + triangle + circles
		Shape2D.drawLine(new Vec2(40, 200), new Vec2(240, 320), Color.RED);
		Shape2D.drawTriangle(new Vec2(320, 60), new Vec2(280, 180), new Vec2(360, 180), Color.GOLD);
		Shape2D.drawCircle(new Vec2(440, 120), 60, Color.PURPLE);
		Shape2D.drawCircleLines(new Vec2(440, 120), 60, Color.BLACK);

		// a marker that follows the mouse
		Shape2D.drawCircle(new Vec2(mouse.x, mouse.y), 8, Color.MAROON);

		Text.draw("wgrender + sokol", 40, 360, 32, Color.DARKGRAY);
		if (Wgr.getPlatform() != "web") // the quit key's hint, as the key: desktop only
			Text.draw("press ESC to quit", 40, 410, 16, Color.GRAY);
		Text.drawFps(40, 12);

		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.getKeyboardState().isPressed(Escape))
			Wgr.requestQuit();
	}
}
