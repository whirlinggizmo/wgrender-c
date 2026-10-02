// wgrender's window example, as a Haxe guest: size, position, fullscreen and monitors.
//
// A port of examples/window.c. Every key here calls a setter that returns whether it
// worked, and the answer depends on where it runs -- which is the example: the same
// call is a no-op on one platform and does the thing on another, and the binding
// reports that rather than hiding it.
//
//   arrows   move the window 50 pixels
//   = / -    grow / shrink by 10%
//   F        toggle fullscreen
//   M        move to the next monitor
//   H        hide for two seconds
//   ESC      quit
//
// On a Wayland desktop (through XWayland) the compositor places windows, so moving
// and changing monitor report "not supported here". On the web the canvas is the
// window: resizing and fullscreen work -- fullscreen because the browser's transient
// activation from the keypress is still live when the frame op runs, though it lands a
// frame later -- while moving and other monitors do not.
//
// `wgr.Window` is where this reads differently from the C. wgrender has getter and
// setter functions throughout; the binding turns the plain reads into properties, so
// `Window.getScreenSize()` and `Window.isFocused()` are fields. Every setter that can be
// refused also has a method form returning whether it worked, because an assignment
// has nowhere to put that -- and this example is entirely about the answer, so it
// uses the methods throughout.
//
// The refusals are all logged, so the properties hide nothing; the methods are for
// acting on a refusal rather than reading about it.
//
// Fullscreen is the one that does not fit that shape, and it is named for it:
// `hasFullscreen` says whether the platform has it at all; `requestFullscreen` returns
// whether there was something to request, not whether
// you got it. The state arrives as an event a frame or more later, so the status line
// says "requested" and the line above it, which reads the property, is what eventually
// says "yes".
//
// The class is `WindowDemo` rather than `Window` for the dull reason that a module
// named `Window` would shadow `wgr.Window` inside itself, and this example says
// `Window` on nearly every line.
import wgr.*;

@:expose("WgrGuest")
class WindowDemo {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 400;
	static inline final STEP = 50;
	static inline final HIDE_FOR = 2.0;

	static var background:Color;
	static var status = "press a key";
	static var hiddenFor = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt));
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "window (wgrender host, Haxe guest)", Resizable);
	}

	static function onInit():Void {
		background = Color.rgba(24, 28, 38, 255);
	}

	static function report(what:String, ok:Bool):Void {
		status = '$what: ${ok ? "done" : "not supported here"}';
	}

	static function handleKeys(dt:Float):Void {
		final keys = Input.getKeyboardState();
		final size = Window.getScreenSize();
		final position = Window.getPosition();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();
		if (keys.isPressed(Left))
			report("move", Window.setPosition(Std.int(position.x) - STEP, Std.int(position.y)));
		if (keys.isPressed(Right))
			report("move", Window.setPosition(Std.int(position.x) + STEP, Std.int(position.y)));
		if (keys.isPressed(Up))
			report("move", Window.setPosition(Std.int(position.x), Std.int(position.y) - STEP));
		if (keys.isPressed(Down))
			report("move", Window.setPosition(Std.int(position.x), Std.int(position.y) + STEP));
		if (keys.isPressed(Equal))
			report("grow", Window.setSize(Std.int(size.x * 1.1), Std.int(size.y * 1.1)));
		if (keys.isPressed(Minus))
			report("shrink", Window.setSize(Std.int(size.x / 1.1), Std.int(size.y / 1.1)));
		if (keys.isPressed(F)) {
			// "requested", not "done": the bool says there was something to request, and
			// the state itself arrives a frame or more later.
			status = Window.requestFullscreen(!Window.isFullscreen()) ? "fullscreen: requested"
				: "fullscreen: not supported here";
		}
		if (keys.isPressed(H)) {
			Window.setVisible(false);
			hiddenFor = HIDE_FOR;
			report("hide for 2 s", true);
		}
		if (hiddenFor > 0.0) {
			hiddenFor -= dt; // it keeps running while hidden
			if (hiddenFor <= 0.0) {
				Window.setVisible(true);
				report("show", true);
			}
		}
		if (keys.isPressed(M))
			report("monitor", Window.setMonitor((Window.getMonitor() + 1) % Window.getMonitorCount()));
	}

	static function onFrame(dt:Float):Void {
		handleKeys(dt);

		final size = Window.getScreenSize();
		final position = Window.getPosition();
		var y = 12;

		Render.beginFrame();
		Render.clearBackground(background);
		Text.draw(Window.hasFullscreen() // ask before offering the key
			? "wgrender window   arrows: move   =/-: size   F: fullscreen   M: next monitor   H: hide"
			: "wgrender window   arrows: move   =/-: size   (no fullscreen here)   M: next monitor   H: hide", 12, y, 16,
			Color.RAYWHITE);
		y += 32;
		Text.draw('window: ${Std.int(size.x)} x ${Std.int(size.y)} at (${Std.int(position.x)}, ${Std.int(position.y)})'
			+ '   fullscreen: ${Window.isFullscreen() ? "yes" : "no"}   focused: ${Window.isFocused() ? "yes" : "no"}', 12, y,
			16, Color.LIGHTGRAY);
		y += 24;
		Text.draw(status, 12, y, 16, Color.GOLD);
		y += 32;
		for (m in 0...Window.getMonitorCount()) {
			final monitorSize = Window.getMonitorSize(m);
			final monitorPosition = Window.getMonitorPosition(m);
			Text.draw('${m == Window.getMonitor() ? ">" : " "} monitor $m "${Window.getMonitorName(m)}": '
				+ '${Std.int(monitorSize.x)} x ${Std.int(monitorSize.y)} '
				+ 'at (${Std.int(monitorPosition.x)}, ${Std.int(monitorPosition.y)})', 12, y, 16, Color.LIGHTGRAY);
			y += 22;
		}
		Render.endFrame();
	}
}
