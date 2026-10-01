// wgrender's meshes example, as a Haxe guest: shapes made in code, not loaded.
//
// A port of examples/meshes.c. Seven generated meshes in a row on a generated floor,
// each its own colour but all sharing one normal-mapped tile material -- which is what
// makes it a test rather than a display: the tiles should lie flat on every surface and
// catch the light the same way, which only happens if each generator got its texture
// coordinates and tangents right.
//
//   O    stop and start the camera
//   ESC  quit
//
// `Mesh.plane/cube/sphere/...` are the binding's names for `wgr_mesh_create_plane` and
// its siblings: static constructors on the type, since `Mesh.create(path)` already has
// the plain name and these make one rather than loading it.
//
// The class is `MeshesDemo` because a module named `Mesh`... is not this one, but the
// rest of the batch reads better with the suffix than without.
import wgr.*;

@:expose("WgrGuest")
class MeshesDemo {
	static inline final SCREEN_WIDTH = 1000;
	static inline final SCREEN_HEIGHT = 600;
	static inline final NORMAL_MAP_PATH = "textures/tiles_normal.png";
	static inline final ASSET_NORMAL_MAP = 1;
	static inline final SHAPE_COUNT = 7;
	static inline final SPACING = 1.4;

	static var scene:Scene;
	static var camera:Camera3D;
	static var materials:Array<Material> = [];
	static var target:Vec3;
	static var orbit = true;
	static var angle = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "meshes (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	/** Each shape, how high its centre sits so it rests on the floor, and its colour. **/
	static function shapes():Array<{mesh:Mesh, y:Float, r:Float, g:Float, b:Float}> {
		return [
			{mesh: Mesh.plane(1.0, 1.0, 4), y: 0.01, r: 0.85, g: 0.85, b: 0.85},
			{mesh: Mesh.cube(0.9, 0.9, 0.9), y: 0.45, r: 0.9, g: 0.3, b: 0.2},
			{mesh: Mesh.sphere(0.5, 24, 48), y: 0.5, r: 0.2, g: 0.55, b: 0.9},
			{mesh: Mesh.cylinder(0.45, 1.0, 40), y: 0.5, r: 0.3, g: 0.8, b: 0.35},
			{mesh: Mesh.cone(0.5, 1.1, 40), y: 0.55, r: 0.95, g: 0.75, b: 0.2},
			{mesh: Mesh.capsule(0.35, 1.2, 16, 40), y: 0.6, r: 0.7, g: 0.35, b: 0.85},
			{mesh: Mesh.torus(0.4, 0.15, 48, 24), y: 0.15, r: 0.9, g: 0.5, b: 0.6}
		];
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		target = new Vec3(0, 0.4, 0);

		camera = new Camera3D(Perspective);
		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setAmbient(Color.WHITE, 0.25);
		final sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.5, -1.0, -0.4));
		sun.setIntensity(3.0);
		scene.add(sun);

		addFloor();
		addShapes();

		if (!GuestAbi.loadAsset(NORMAL_MAP_PATH, ASSET_NORMAL_MAP))
			Log.error('failed to queue asset: $NORMAL_MAP_PATH');
		Debug.enableFps(12, 10, 16);
	}

	static function addFloor():Void {
		final plane = Mesh.plane(12.0, 12.0, 0);
		final floor = new Model(plane);
		plane.release(); // the model holds its own reference
		final ground = new Material(Pbr);
		ground.setBaseColor(0.06, 0.06, 0.07, 1.0);
		ground.setMetallic(0.0);
		ground.setRoughness(0.9);
		floor.setMaterial(-1, ground); // -1: every slot
		ground.release();
		scene.add(floor);
	}

	static function addShapes():Void {
		final all = shapes();
		for (i in 0...SHAPE_COUNT) {
			final shape = all[i];
			final x = (i - (SHAPE_COUNT - 1) * 0.5) * SPACING;
			final model = new Model(shape.mesh);
			shape.mesh.release();
			model.setPosition(x, shape.y, 0);

			final material = new Material(Pbr);
			material.setBaseColor(shape.r, shape.g, shape.b, 1.0);
			material.setMetallic(0.0);
			material.setRoughness(0.45);
			material.setVec2("normal_texture_scale", 2.0, 2.0); // the tiles repeat
			model.setMaterial(0, material);
			material.release(); // the model keeps it alive
			materials.push(material);
			scene.add(model);
		}
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		if (id != ASSET_NORMAL_MAP)
			return;
		final texture = new Texture(path);
		for (material in materials)
			material.setNormalTexture(texture);
		texture.release(); // the materials hold their own references
	}

	static function onFrame(dt:Float):Void {
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
		if (Input.isKeyPressed(O))
			orbit = !orbit;
		if (orbit)
			angle += dt * 0.2;
		camera.setView(new Vec3(9.0 * Math.sin(angle), 3.5, 9.0 * Math.cos(angle)), target);

		Render.beginFrame();
		Render.clearBackground(Color.rgba(20, 22, 28, 255));
		scene.draw();
		Text.draw("wgrender generated meshes: plane, cube, sphere, cylinder, cone, capsule, torus", 12, 36, 20,
			Color.RAYWHITE);
		Text.draw((orbit ? "O: stop the camera" : "O: turn the camera") + (Wgr.getPlatform() == "web" ? "" : "   ESC: quit"), 12, 64, 16,
			Color.LIGHTGRAY);
		Render.endFrame();
	}
}
