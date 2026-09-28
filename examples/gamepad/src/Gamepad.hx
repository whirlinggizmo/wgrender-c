// wgrender's gamepad example, as a Haxe guest: every connected pad, live.
//
// A port of examples/gamepad.c. Up to four pads side by side: the name, both sticks
// (the dot is where the stick is *after* the dead zone, the ring is its reach), the
// triggers as bars, and the buttons laid out like an Xbox-style pad — lit while held
// and flashed white on the frame they are pressed, which is the difference between
// `Down` and `Pressed`.
//
//   SOUTH on any pad   cycle the dead zone: 0.15, 0, 0.3
//   ESC                quit
//
// In a browser, press a button on the pad first: a page sees no gamepads at all until
// the user has used one.
import wgr.*;

@:expose("WgrGuest")
class Gamepad {
	static inline final SCREEN_WIDTH = 680;
	static inline final SCREEN_HEIGHT = 720;
	static final DEADZONES = [0.15, 0.0, 0.3];
	static inline final REACH = 34.0;
	static inline final TRIGGER_HEIGHT = 60.0;

	static var background:Color;
	static var idle:Color;
	static var outline:Color;
	static var deadzone = 0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {});
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "gamepad (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		background = Color.rgba(20, 22, 30, 255);
		idle = Color.rgba(60, 66, 80, 255);
		outline = Color.rgba(90, 98, 118, 255);
	}

	/** White the frame it goes down, gold while held, dark otherwise. **/
	static function buttonColor(pad:Int, button:GamepadButton):Color {
		return switch Input.getGamepadButton(pad, button) {
			case Pressed: Color.WHITE;
			case Down: Color.GOLD;
			case _: idle;
		}
	}

	static function drawButton(pad:Int, button:GamepadButton, x:Float, y:Float, r:Float):Void {
		Shape2D.drawCircle(new Vec2(x, y), r, buttonColor(pad, button));
	}

	static function drawStick(pad:Int, xAxis:GamepadAxis, yAxis:GamepadAxis, click:GamepadButton, cx:Float, cy:Float):Void {
		Shape2D.drawCircleLines(new Vec2(cx, cy), REACH, outline);
		if (Input.getGamepadButton(pad, click) != Up)
			Shape2D.drawCircle(new Vec2(cx, cy), REACH, idle);
		Shape2D.drawCircle(new Vec2(cx + Input.getGamepadAxis(pad, xAxis) * REACH,
			cy + Input.getGamepadAxis(pad, yAxis) * REACH), 9.0, Color.SKYBLUE);
	}

	static function drawTrigger(pad:Int, axis:GamepadAxis, x:Float, y:Float):Void {
		final value = Input.getGamepadAxis(pad, axis);
		Shape2D.drawRectangleLines(x, y, 16.0, TRIGGER_HEIGHT, outline);
		Shape2D.drawRectangle(x, y + TRIGGER_HEIGHT * (1.0 - value), 16.0, TRIGGER_HEIGHT * value, Color.ORANGE);
	}

	static function drawPad(pad:Int, x:Float, y:Float):Void {
		Text.draw('pad $pad', Std.int(x), Std.int(y), 20, Color.RAYWHITE);
		if (!Input.isGamepadConnected(pad)) {
			Text.draw("not connected", Std.int(x), Std.int(y) + 26, 16, Color.GRAY);
			return;
		}
		Text.draw(Input.getGamepadName(pad).substr(0, 40), Std.int(x), Std.int(y) + 26, 14, Color.LIGHTGRAY);

		// shoulders and triggers
		drawTrigger(pad, LeftTrigger, x + 10.0, y + 56.0);
		drawTrigger(pad, RightTrigger, x + 274.0, y + 56.0);
		Shape2D.drawRectangle(x + 34.0, y + 60.0, 60.0, 14.0, buttonColor(pad, LeftBumper));
		Shape2D.drawRectangle(x + 206.0, y + 60.0, 60.0, 14.0, buttonColor(pad, RightBumper));

		// sticks, d-pad, face buttons, middle buttons
		drawStick(pad, LeftX, LeftY, LeftStick, x + 70.0, y + 130.0);
		drawStick(pad, RightX, RightY, RightStick, x + 190.0, y + 210.0);
		Shape2D.drawRectangle(x + 102.0, y + 180.0, 18.0, 18.0, buttonColor(pad, DpadUp));
		Shape2D.drawRectangle(x + 102.0, y + 220.0, 18.0, 18.0, buttonColor(pad, DpadDown));
		Shape2D.drawRectangle(x + 82.0, y + 200.0, 18.0, 18.0, buttonColor(pad, DpadLeft));
		Shape2D.drawRectangle(x + 122.0, y + 200.0, 18.0, 18.0, buttonColor(pad, DpadRight));
		drawButton(pad, North, x + 240.0, y + 106.0, 12.0);
		drawButton(pad, South, x + 240.0, y + 154.0, 12.0);
		drawButton(pad, West, x + 216.0, y + 130.0, 12.0);
		drawButton(pad, East, x + 264.0, y + 130.0, 12.0);
		drawButton(pad, Back, x + 126.0, y + 130.0, 8.0);
		drawButton(pad, Guide, x + 150.0, y + 110.0, 10.0);
		drawButton(pad, Start, x + 174.0, y + 130.0, 8.0);

		Text.draw('L ${signed(Input.getGamepadAxis(pad, LeftX))} ${signed(Input.getGamepadAxis(pad, LeftY))}'
			+ '  R ${signed(Input.getGamepadAxis(pad, RightX))} ${signed(Input.getGamepadAxis(pad, RightY))}',
			Std.int(x), Std.int(y) + 262, 14, Color.LIGHTGRAY);
	}

	static function onFrame(dt:Float):Void {
		for (pad in 0...Input.MAX_GAMEPADS) {
			if (Input.getGamepadButton(pad, South) == Pressed) {
				deadzone = (deadzone + 1) % DEADZONES.length;
				Input.setGamepadDeadzone(DEADZONES[deadzone]);
			}
		}

		Render.beginFrame();
		Render.clearBackground(background);
		Text.draw("wgrender + sokol — gamepads", 12, 36, 24, Color.RAYWHITE);
		Text.draw('dead zone ${fixed(DEADZONES[deadzone], 2)} (SOUTH changes it)   '
			+ "web: press a pad button first", 12, 70, 16, Color.LIGHTGRAY);
		for (pad in 0...Input.MAX_GAMEPADS)
			drawPad(pad, 20.0 + (pad % 2) * 320.0, 110.0 + Std.int(pad / 2) * 300.0);
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
	}

	/** An axis reading, always with its sign, as the C's "%+.2f" prints it. **/
	static inline function signed(value:Float):String
		return (value < 0 ? "" : "+") + fixed(value, 2);

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
