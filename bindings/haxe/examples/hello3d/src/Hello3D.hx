// wgrender's hello3d, as a Haxe guest: an orbiting camera over immediate-mode 3D
// primitives, with a 2D text overlay. A port of examples/hello3d.c, line for line
// where the two languages agree.
//
// It loads nothing, so it is the size floor for this architecture: whatever a Haxe
// game costs over the wgrender host when the game itself is a few dozen lines.
import wgr.*;

@:expose("WgrGuest")
class Hello3D {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 700;

	static inline final ORBIT_RADIUS = 16.0;
	static inline final ORBIT_SPEED = 0.4;
	static inline final CAMERA_HEIGHT = 9.0;

	// Built in onInit, not here: a `static final` runs when this module loads, which
	// on the web is before the host exists. Raw says so if you forget.
	static var background:Color;

	static final LOOK_AT = new Vec3(0, 1, 0);
	static final ORIGIN = new Vec3(0, 0, 0);

	static var camera:Camera3D;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {}); // loads nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "hello3d (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		camera = new Camera3D(Perspective); // default fov: pi/4
		camera.setView(new Vec3(14, 8, 14), LOOK_AT);
		camera.setActive();
		background = Color.rgba(28, 28, 38);
		Debug.enableFps(12, 10, 16);
	}

	static function onFrame(dt:Float):Void {
		// orbit the camera around the origin
		final t = Wgr.getTime();
		camera.setView(new Vec3(Math.cos(t * ORBIT_SPEED) * ORBIT_RADIUS, CAMERA_HEIGHT,
			Math.sin(t * ORBIT_SPEED) * ORBIT_RADIUS), LOOK_AT);

		Render.beginFrame();
		Render.clearBackground(background);

		Render.beginMode3D();
		Shape3D.drawGrid(20, 1, Color.DARKGRAY);

		// axes
		Shape3D.drawLine(ORIGIN, new Vec3(5, 0, 0), Color.RED);
		Shape3D.drawLine(ORIGIN, new Vec3(0, 5, 0), Color.GREEN);
		Shape3D.drawLine(ORIGIN, new Vec3(0, 0, 5), Color.BLUE);

		Shape3D.drawCube(new Vec3(0, 1, 0), new Vec3(2, 2, 2), Color.SKYBLUE);
		Shape3D.drawCubeWires(new Vec3(0, 1, 0), new Vec3(2.02, 2.02, 2.02), Color.DARKBLUE);
		Shape3D.drawSphere(new Vec3(5, 1.5, 0), 1.5, Color.GOLD);
		Shape3D.drawSphere(new Vec3(-5, 1.5, 0), 1.5, Color.MAROON);
		Render.endMode3D();

		Text.draw("wgrender + sokol — 3D", 12, 36, 24, Color.RAYWHITE);
		Text.draw("orbiting camera3d, depth-tested sokol_gl", 12, 70, 16, Color.LIGHTGRAY);

		Render.endFrame();

		// the whole keyboard at once, as the C does. On js this is a view into the
		// wasm heap that is only good for this frame, which is why it is read here.
		final keyboard = Input.getKeyboardState();
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keyboard.isPressed(Escape))
			Wgr.requestQuit();
	}
}
