// wgrender's tick, as a Haxe guest: fixed-rate simulation against rendering.
//
// A deliberately slow 10 Hz tick moves two squares at the same speed. The top one is
// drawn at its latest tick position, so it visibly steps; the bottom one is drawn
// between its last two tick positions using the tick fraction, so it moves smoothly
// at any frame rate. SPACE presses counted inside the tick and inside the frame
// always match, because each press is seen by exactly one tick.
//
// A port of examples/tick.c. It is the example that made the guest ABI grow a fifth
// op: the ABI mirrored wg-vf's table, where `tick` is the render-rate callback that a
// renderer calls `frame`, so wgrender's fixed-rate tick had no counterpart.
import wgr.*;

@:expose("WgrGuest")
class Tick {
	static inline final SCREEN_WIDTH = 800;
	static inline final SCREEN_HEIGHT = 450;
	static inline final TICK_HZ = 10;
	static inline final SQUARE = 40;
	static inline final LEFT = 40;
	static inline final RIGHT = SCREEN_WIDTH - 80;
	static inline final SPEED = 240.0; // pixels per second

	// simulation state: the last two tick positions
	static var previousX = 0.0;
	static var x = 0.0;
	static var ticks = 0;
	static var tickPresses = 0;
	static var framePresses = 0;
	static var background:Color;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt));
		GuestAbi.registerTick(onTick, TICK_HZ);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "tick (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		background = Color.rgba(24, 26, 34);
		x = previousX = LEFT;
	}

	static function onTick(dt:Float):Void {
		previousX = x;
		x += SPEED * dt;
		if (x > RIGHT) {
			x = LEFT;
			previousX = x; // don't interpolate across the wrap
		}
		ticks++;
		if (Input.getKey(Space) == Pressed)
			tickPresses++;
	}

	static function onFrame(dt:Float):Void {
		final keyboard = Input.getKeyboardState();
		if (keyboard.isPressed(Space))
			framePresses++;
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keyboard.isPressed(Escape))
			Wgr.requestQuit();

		final fraction = GuestAbi.tickFraction();

		Render.beginFrame();
		Render.clearBackground(background);

		Text.draw("wgrender tick: 10 Hz simulation, rendered every frame", 20, 20, 20, Color.RAYWHITE);
		Text.draw('frame dt ${fixed(dt, 4)} s   tickFraction ${fixed(fraction, 2)}   ticks $ticks',
			20, 50, 16, Color.LIGHTGRAY);
		Text.draw('SPACE presses: tick $tickPresses, frame $framePresses', 20, 74, 16, Color.LIGHTGRAY);

		Text.draw("raw tick position", 20, 130, 16, Color.GRAY);
		Shape2D.drawRectangle(Std.int(x), 155, SQUARE, SQUARE, Color.ORANGE);

		Text.draw("interpolated with the tick fraction", 20, 250, 16, Color.GRAY);
		Shape2D.drawRectangle(Std.int(previousX + (x - previousX) * fraction), 275, SQUARE, SQUARE, Color.SKYBLUE);

		Text.drawFps(20, SCREEN_HEIGHT - 30);
		Render.endFrame();
	}

	/** C's "%.Nf"; Haxe has no printf and the numbers here are worth reading. **/
	static function fixed(value:Float, places:Int):String {
		final scale = Math.pow(10, places);
		final rounded = Math.round(value * scale) / scale;
		final text = Std.string(rounded);
		final dot = text.indexOf(".");
		if (dot < 0)
			return text + "." + StringTools.rpad("", "0", places);
		return StringTools.rpad(text, "0", dot + 1 + places);
	}
}
