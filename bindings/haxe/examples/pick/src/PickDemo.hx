// wgrender's picking example, as a Haxe guest: all three drawable kinds, ray-picked.
//
// A port of examples/pick.c. `scene.pick` does a world-AABB broadphase and then a
// narrow phase per kind: shapes get exact ray/cube and ray/sphere tests, models exact
// ray/triangle against the bind-pose mesh, and sprites test the billboard quad and —
// with alpha-test picking on — reject transparent texels through a CPU mask built from
// the texture on demand. So clicking the hole in the logo misses it.
//
// The camera is fixed, so aiming is predictable, and the readout says what was hit,
// where in world and local space, and how far.
//
//   click  a cube, a sphere, a sprite or a model
//   ESC    quit
//
// The class is `PickDemo` because a module named `Pick` would shadow `wgr.Pick`.
import wgr.*;

@:expose("WgrGuest")
class PickDemo {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 700;
	static inline final LOGO_PATH = "sprites/logo/wg-logo-bw-alpha.png";
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";

	static var background:Color;
	static var scene:Scene;
	static var camera:Camera3D;
	static var cube:Shape3D;
	static var sphere:Shape3D;
	static var sprite:Sprite3D;
	static var model:Model;
	static var selected:Handle = Handle.NONE;
	static var last:PickResult;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {}); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "pick (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(24, 26, 34, 255);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(11.0, 9.0, 11.0), new Vec3(0, 2.0, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);

		// scenes start unlit: a sun and some ambient, so the model can be seen
		final sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.6, -1.0, -0.5));
		sun.setIntensity(3.0);
		scene.add(sun);
		scene.setAmbient(Color.WHITE, 0.3);

		cube = new Shape3D();
		cube.setCube(new Vec3(2.0, 2.0, 2.0));
		cube.setTransform(new Vec3(-3.5, 1.0, 0), new Vec3(0, 0.6, 0), Vec3.ONE);
		cube.setColor(Color.ORANGE);
		scene.add(cube);

		sphere = new Shape3D();
		sphere.setSphere(1.5);
		sphere.setPosition(3.5, 1.5, 0);
		sphere.setColor(Color.GOLD);
		scene.add(sphere);

		makeLogo();
		makeModel(); // loads on create: drawn once it has loaded
		Debug.enableFps(12, 10, 16);
	}

	// The logo, drawn and picked once its texture has loaded.
	static function makeLogo():Void {
		final texture = new Texture(LOGO_PATH);
		sprite = new Sprite3D(texture);
		texture.release(); // the sprite holds its own reference
		sprite.setSize(4.0);
		sprite.setFacing(Camera);
		sprite.setTint(Color.WHITE);
		sprite.setPosition(0, 3.0, 4.0);
		// the transparent parts of the logo let the click through
		sprite.setPickAlphaTest(true, 0.5);
		scene.add(sprite, 1);
	}

	static function makeModel():Void {
		final mesh = new Mesh(CHARACTER_PATH);
		model = new Model(mesh);
		mesh.release(); // the model holds its own reference to the mesh
		if (model.isNone())
			return;
		model.setPosition(0, 0, -4.0);
		model.setTint(Color.RAYWHITE);
		scene.add(model);
	}

	static function kindName(handle:Handle):String {
		return switch handle.getKind() {
			case Shape3D: "shape";
			case Sprite3D: "sprite3d";
			case Model: "model";
			case _: "?";
		}
	}

	/** Shapes can be recoloured to show selection; the other kinds just get reported. **/
	static function restingColor(shape:Handle):Color {
		if (shape == cube)
			return Color.ORANGE;
		if (shape == sphere)
			return Color.GOLD;
		return Color.RAYWHITE;
	}

	static function select(hit:Handle):Void {
		if (hit == selected)
			return;
		// The kind is the test, not a list of known handles: only a shape can be
		// recoloured, and `kind` answers that for any handle at all.
		if (selected.getKind() == Shape3D)
			(selected : Shape3D).setColor(restingColor(selected));
		selected = hit;
		if (selected.getKind() == Shape3D)
			(selected : Shape3D).setColor(Color.RAYWHITE);
	}

	static function onFrame(dt:Float):Void {
		final mouse = Input.getMouseState();
		if (mouse.left == ButtonState.Pressed) {
			last = scene.pick(mouse.x, mouse.y);
			select(last.hit ? last.handle : Handle.NONE);
		}

		Render.beginFrame();
		Render.clearBackground(background);
		Render.beginMode3D();
		Shape3D.drawGrid(24, 1.0, Color.DARKGRAY);
		Render.endMode3D();
		scene.draw();

		Text.draw("wgrender + sokol — picking", 12, 36, 24, Color.RAYWHITE);
		Text.draw("click cube / sphere / sprite / model", 12, 70, 16, Color.LIGHTGRAY);
		if (last != null && last.hit) {
			Text.draw('hit ${kindName(last.handle)} (handle ${last.handle})', 12, 94, 16, Color.LIME);
			Text.draw('world ${vec(last.pointWorld)}   dist ${fixed(last.distance, 2)}', 12, 114, 16,
				Color.LIGHTGRAY);
			Text.draw('local ${vec(last.pointLocal)}', 12, 134, 16, Color.LIGHTGRAY);
		} else {
			Text.draw("no hit", 12, 94, 16, Color.LIGHTGRAY);
		}
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
	}

	static inline function vec(v:Vec3):String
		return '${fixed(v.x, 2)}, ${fixed(v.y, 2)}, ${fixed(v.z, 2)}';

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
