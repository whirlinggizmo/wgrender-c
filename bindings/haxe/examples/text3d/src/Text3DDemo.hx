// wgrender's text3d example, as a Haxe guest: text in the world, shapes, and picking.
//
// A port of examples/text3d.c.
//
//   - camera-facing labels above a cube, a sphere and a rectangle
//   - a sign: text with Free facing, turned by its own transform rather than the camera
//   - circle outlines on the ground, and a line-strip spiral built a point at a time
//   - hover: `scene.pick` finds what is under the mouse, labels included, and the
//     pick statistics for that query are drawn beside it
//   - P toggles whether the cube is pickable, which takes it out of the query entirely
//
//   ESC  quit
//
// The class is `Text3DDemo` because a module named `Text3D` would shadow `wgr.Text3D`
// inside itself.
import wgr.*;

@:expose("WgrGuest")
class Text3DDemo {
	static inline final SCREEN_WIDTH = 1000;
	static inline final SCREEN_HEIGHT = 640;
	static inline final FONT_PATH = "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf";
	static inline final SPIRAL_POINTS = 160;

	static var background:Color;
	static var grey:Color;
	static var gold:Color;
	static var teal:Color;
	static var rose:Color;
	static var ringColor:Color;

	static var scene:Scene;
	static var camera:Camera3D;
	static var font:Font;
	static var cube:Shape3D;
	static var sphere:Shape3D;
	static var panel:Shape3D;
	static var sign:Text3D;
	static var labels:Array<Text3D> = [];
	static var elapsed = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt)); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "text3d (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(22, 24, 30, 255);
		grey = Color.rgba(60, 64, 76, 255);
		gold = Color.rgba(230, 180, 60, 255);
		teal = Color.rgba(60, 190, 180, 255);
		rose = Color.rgba(220, 90, 120, 255);
		ringColor = Color.rgba(120, 130, 160, 255);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 4.0, 9.0), new Vec3(0, 0.8, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);

		cube = new Shape3D();
		cube.setCube(new Vec3(1.2, 1.2, 1.2));
		cube.setPosition(-3, 0.6, 0);
		scene.add(cube);

		sphere = new Shape3D();
		sphere.setSphere(0.7);
		sphere.setPosition(0, 0.7, 0);
		scene.add(sphere);

		panel = new Shape3D();
		panel.setRectangle(1.4, 1.0);
		panel.setTransform(new Vec3(3, 0.8, 0), new Vec3(0, -0.5, 0), Vec3.one());
		scene.add(panel);

		addRings();
		addSpiral();

		labels.push(addLabel("cube", new Vec3(-3, 1.7, 0)));
		labels.push(addLabel("sphere", new Vec3(0, 1.9, 0)));
		labels.push(addLabel("rectangle", new Vec3(3, 1.8, 0)));

		// Free facing: oriented by its own rotation, like a sign, rather than turned
		// to the camera the way the labels are.
		sign = new Text3D(Handle.NONE); // its font is set at the end of init
		sign.setText("wgrender text3d");
		sign.setFontSize(0.6);
		sign.setFacing(Free);
		sign.setColor(gold);
		scene.add(sign);

		font = new Font(FONT_PATH); // the built-in font until it has loaded
		for (label in labels)
			label.setFont(font);
		sign.setFont(font);
	}

	/** Rings lying on the ground under each object, and not pickable. **/
	static function addRings():Void {
		for (i in 0...3) {
			final ring = new Shape3D();
			ring.setCircle(1.0);
			ring.setTransform(new Vec3(-3.0 + 3.0 * i, 0.01, 0), new Vec3(-Math.PI / 2, 0, 0), Vec3.one());
			ring.setColor(ringColor);
			ring.setPickable(false);
			scene.add(ring);
		}
	}

	static function addSpiral():Void {
		final spiral = new Shape3D();
		spiral.setLineStrip();
		for (i in 0...SPIRAL_POINTS + 1) {
			final t = i / SPIRAL_POINTS;
			final a = t * 2 * Math.PI * 4.0;
			spiral.addPoint(new Vec3(Math.cos(a) * (0.2 + t), t * 2.5, Math.sin(a) * (0.2 + t)));
		}
		spiral.setPosition(0, 0, -3);
		spiral.setColor(teal);
		scene.add(spiral);
	}

	static function addLabel(text:String, position:Vec3):Text3D {
		final label = new Text3D(Handle.NONE); // its font is set at the end of init
		label.setText(text);
		label.setFontSize(0.35);
		label.setPosition(position);
		label.setColor(Color.RAYWHITE);
		scene.add(label);
		return label;
	}

	static function nameOf(handle:Handle):String {
		if (handle == cube)
			return "cube";
		if (handle == sphere)
			return "sphere";
		if (handle == panel)
			return "rectangle";
		if (handle == sign)
			return "sign";
		return "label";
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		final mouse = Input.getMouseState();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();
		if (keys.isPressed(P))
			cube.setPickable(!cube.isPickable());

		elapsed += dt;
		cube.setTransform(new Vec3(-3, 0.6, 0), new Vec3(0, elapsed * 0.7, 0), Vec3.one());
		sign.setTransform(new Vec3(0, 3.2, -3), new Vec3(0, Math.sin(elapsed * 0.6) * 0.6, 0));

		// hover: the nearest pickable object under the mouse
		Pick.resetStats();
		final pick = scene.pick(mouse.x, mouse.y);
		final hovered = pick.hit ? pick.handle : Handle.NONE;
		cube.setColor(hovered == cube ? Color.WHITE : gold);
		sphere.setColor(hovered == sphere ? Color.WHITE : rose);
		panel.setColor(hovered == panel ? Color.WHITE : teal);
		for (label in labels)
			label.setColor(hovered == label ? gold : Color.RAYWHITE);
		final stats = Pick.getStats();

		Render.beginFrame();
		Render.clearBackground(background);
		Render.beginMode3D();
		Shape3D.drawGrid(16, 1.0, grey);
		Render.endMode3D();
		scene.draw();

		font.draw("wgrender text3d: text in 3D, shapes, picking", 12, 10, 20, Color.RAYWHITE);
		font.draw('hover: ${pick.hit ? nameOf(pick.handle) : "nothing"}   '
			+ '[P] cube pickable: ${cube.isPickable() ? "yes" : "no"}', 12, 36, 16, Color.LIGHTGRAY);
		font.draw('pick stats: ${stats.broadphaseTests} box tests (${stats.broadphaseRejects} rejected), '
			+ '${stats.narrowphaseTests} exact tests, ${stats.narrowphaseHits} hits', 12, 56, 16, Color.LIGHTGRAY);
		font.drawFps(12, 80, 16, Color.LIME);
		Render.endFrame();
	}
}
