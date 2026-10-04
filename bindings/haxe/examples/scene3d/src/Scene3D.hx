// wgrender's scene3d example, as a Haxe guest: retained shapes, and picking one.
//
// A port of examples/scene3d.c. A ring of cubes, a sphere and a spinner live in the
// scene rather than being drawn each frame, so `scene.draw()` is the whole render and
// `scene.pick(x, y)` answers what is under the mouse. Click a shape to select it; the
// selected one turns white and the previous one goes back to its own colour.
//
// The spinner is on layer 1 and everything else on layer 0, so it draws over them.
//
//   click  select a shape
//   ESC    quit
import wgr.*;

@:expose("WgrGuest")
class Scene3D {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 700;
	static inline final RING = 8;

	static var background:Color;
	static var scene:Scene;
	static var camera:Camera3D;
	static var target:Vec3;
	static var spinner:Shape3D;
	static var sphere:Shape3D;
	static var ring:Array<Shape3D> = [];
	static var selected:Shape3D = Handle.NONE;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt));
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "scene3d (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		background = Color.rgba(24, 26, 34, 255);
		target = new Vec3(0, 1.0, 0);
		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(16.0, 11.0, 16.0), target);
		scene = new Scene();
		scene.setActiveCamera(camera);

		for (i in 0...RING) {
			final a = i * 2 * Math.PI / RING;
			final cube = new Shape3D();
			cube.setCube(new Vec3(1.5, 1.5, 1.5));
			cube.setTransform(new Vec3(Math.cos(a) * 6.0, 0.75, Math.sin(a) * 6.0), new Vec3(0, a, 0), Vec3.one());
			cube.setColor(defaultColorFor(i));
			scene.add(cube);
			ring.push(cube);
		}

		sphere = new Shape3D();
		sphere.setSphere(1.5);
		sphere.setPosition(0, 2.5, 0);
		sphere.setColor(Color.GOLD);
		scene.add(sphere);

		spinner = new Shape3D();
		spinner.setCube(new Vec3(2.0, 2.0, 2.0));
		spinner.setColor(Color.LIME);
		scene.add(spinner, 1); // layer 1: over the rest

		Debug.enableFps(12, 10, 16);
	}

	static inline function defaultColorFor(index:Int):Color
		return index % 2 == 1 ? Color.SKYBLUE : Color.ORANGE;

	/** What a shape goes back to when it stops being the selected one. **/
	static function restingColor(shape:Shape3D):Color {
		if (shape == spinner)
			return Color.LIME;
		if (shape == sphere)
			return Color.GOLD;
		for (i in 0...RING)
			if (ring[i] == shape)
				return defaultColorFor(i);
		return Color.RAYWHITE;
	}

	static function select(hit:Shape3D):Void {
		if (hit == selected)
			return;
		if (!selected.isNone())
			selected.setColor(restingColor(selected));
		selected = hit;
		if (!selected.isNone())
			selected.setColor(Color.RAYWHITE);
	}

	static function onFrame(dt:Float):Void {
		final t = Wgr.getTime();
		final mouse = Input.getMouseState();

		camera.setView(new Vec3(Math.cos(t * 0.35) * 18.0, 11.0, Math.sin(t * 0.35) * 18.0), target);
		spinner.setTransform(new Vec3(0, 5.0, 0), new Vec3(t * 1.3, t * 0.9, 0), Vec3.one());

		if (mouse.left == ButtonState.Pressed) {
			final pick = scene.pick(mouse.x, mouse.y);
			// A pick gives an untyped Handle; it compares to a typed one as it is.
			select(pick.hit ? pick.handle : Handle.NONE);
		}

		Render.beginFrame();
		Render.clearBackground(background);

		Render.beginMode3D();
		Shape3D.drawGrid(24, 1.0, Color.DARKGRAY);
		Render.endMode3D();

		scene.draw();

		Text.draw("wgrender + sokol — scene pick", 12, 36, 24, Color.RAYWHITE);
		Text.draw("click a shape to select it", 12, 70, 16, Color.LIGHTGRAY);
		Text.draw(selected.isNone() ? "selected: none" : 'selected handle: $selected', 12, 94, 16, Color.LIGHTGRAY);
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
	}
}
