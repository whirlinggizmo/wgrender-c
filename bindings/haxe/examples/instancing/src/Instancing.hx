// wgrender's instancing example, as a Haxe guest: many models that share a mesh.
//
// A port of examples/instancing.c, and the thing to notice is that nothing here asks
// for instancing. It is what wgrender does when models agree on everything but where
// they stand: a field of 400 cubes shares one mesh and one material and differs only
// in transform and tint, so it goes up as one draw. Six characters share one skinned
// mesh and animate out of step, so their joints are per instance. A few cubes are
// see-through and keep their back-to-front order. The sun casts, so the same batching
// happens again into its shadow map.
//
//   SPACE  give every cube its own material -- the same picture, drawn one at a time
//   ESC    quit
import wgr.*;

@:expose("WgrGuest")
class Instancing {
	static inline final SCREEN_WIDTH = 1024;
	static inline final SCREEN_HEIGHT = 720;
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";

	static inline final FIELD_SIDE = 20;
	static inline final FIELD_COUNT = FIELD_SIDE * FIELD_SIDE;
	static inline final WALKERS = 6;
	static inline final GLASS = 5;

	static var scene:Scene;
	static var camera:Camera3D;
	static var target:Vec3;
	static var cubes:Array<Model> = [];
	static var walkers:Array<Model> = [];
	static var sharedMaterial:Material;
	static var ownMaterials = false;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt)); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "instancing (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		target = new Vec3(0, 1.0, 0);
		camera = new Camera3D(Perspective);
		scene = new Scene();
		scene.setActiveCamera(camera);

		final sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.5, -1.0, -0.4));
		sun.setIntensity(3.0);
		sun.setShadowDistance(60.0);
		sun.setCastsShadows(true); // the depth pass batches the same way
		scene.add(sun);
		scene.setAmbient(Color.rgba(160, 180, 220, 255), 0.35);
		Debug.enableFps(12, 10, 16);

		addFloor();
		addField();
		addWalkers();
	}

	/** Something for the shadows to land on. **/
	static function addFloor():Void {
		final mesh = Mesh.plane(60.0, 60.0, 0);
		final material = new Material(Pbr);
		material.setBaseColor(0.45, 0.47, 0.5, 1.0);
		material.setRoughness(0.9);
		final floor = new Model(mesh);
		floor.setMaterial(-1, material);
		floor.setPosition(0, -0.6, 0);
		scene.add(floor);
		mesh.release();
		material.release();
	}

	static function fieldTint(i:Int, alpha:Int):Color {
		final hue = i / FIELD_COUNT;
		return Color.rgba(Std.int(120 + 135 * Math.sin(hue * 6.28)), Std.int(120 + 135 * Math.sin(hue * 6.28 + 2.1)),
			Std.int(120 + 135 * Math.sin(hue * 6.28 + 4.2)), alpha);
	}

	static function addField():Void {
		// One mesh and one material for the lot; only the transform and tint differ.
		final cube = Mesh.cube(0.6, 0.6, 0.6);
		sharedMaterial = new Material(Pbr);
		sharedMaterial.setRoughness(0.5);
		for (i in 0...FIELD_COUNT) {
			final model = new Model(cube);
			model.setTint(fieldTint(i, 255));
			scene.add(model);
			cubes.push(model);
		}
		setMaterials(false);

		// See-through copies of the same cube: same mesh, same material, alpha in the
		// tint, and they keep their back-to-front order rather than joining the batch.
		for (i in 0...GLASS) {
			final glass = new Model(cube);
			glass.setMaterial(-1, sharedMaterial);
			glass.setTint(Color.rgba(255, 255, 255, 110));
			glass.setTransform(new Vec3(i * 2.0 - 4.0, 5.2, 5.0), Vec3.ZERO, new Vec3(2, 2, 2));
			scene.add(glass);
		}
		cube.release();
	}

	/** Six walkers sharing one skinned mesh, each at its own point in the walk. **/
	static function addWalkers():Void {
		final mesh = new Mesh(CHARACTER_PATH); // the same resource for every walker
		for (i in 0...WALKERS) {
			final walker = new Model(mesh);
			walker.setTransform(new Vec3(i * 2.4 - 6.0, 0.0, -2.0), new Vec3(0, Math.PI, 0), Vec3.ONE);
			walker.setAnimation(3);
			walker.setAnimationLoop(true);
			scene.add(walker);
			walkers.push(walker);
		}
		mesh.release(); // the walkers hold their own references
	}

	/** One material for every cube, or one each: the same picture, batched or not. **/
	static function setMaterials(own:Bool):Void {
		for (cube in cubes) {
			if (own) {
				final material = new Material(Pbr);
				material.setRoughness(0.5);
				cube.setMaterial(-1, material);
				material.release(); // the model keeps its reference
			} else {
				cube.setMaterial(-1, sharedMaterial);
			}
		}
		ownMaterials = own;
	}

	static function onFrame(dt:Float):Void {
		final t = Wgr.getTime();
		camera.setView(new Vec3(Math.sin(t * 0.15) * 22.0, 12.0, Math.cos(t * 0.15) * 22.0), target);

		for (i in 0...FIELD_COUNT) {
			final x = ((i % FIELD_SIDE) - FIELD_SIDE * 0.5 + 0.5) * 1.5;
			final z = (Std.int(i / FIELD_SIDE) - FIELD_SIDE * 0.5 + 0.5) * 1.5;
			final wave = Math.sin(t * 1.5 + x * 0.6 + z * 0.4);
			// Well clear of the floor, so every cube throws its own shadow onto it.
			cubes[i].setTransform(new Vec3(x, 2.4 + wave * 0.5, z), new Vec3(0, t * 0.3 + i, 0), Vec3.ONE);
		}
		// The same walk, out of step: a shared mesh, but each its own pose.
		for (i in 0...WALKERS)
			walkers[i].setAnimationTime(t + i * 0.35);

		Render.beginFrame();
		Render.clearBackground(Color.rgba(28, 30, 38, 255));
		scene.draw();
		Text.draw("wgrender instancing: models that share a mesh and a material go up as one draw", 12, 36, 20,
			Color.RAYWHITE);
		Text.draw('$FIELD_COUNT cubes, ${ownMaterials ? "a material each (one draw each)" : "one material (one draw)"}'
			+ "   SPACE toggles" + (Wgr.getPlatform() == "web" ? "" : "   ESC quit"), 12, 64, 16, Color.LIGHTGRAY);
		Render.endFrame();

		final keys = Input.getKeyboardState();
		if (keys.isPressed(Space))
			setMaterials(!ownMaterials);
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();
	}
}
