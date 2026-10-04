// wgrender's model example, as a Haxe guest: a glTF model, loaded async and animated.
//
// A port of examples/model.c. The order is the point: the mesh loads on create, so the
// Model is made from it and added to the scene straight away, while the mesh is still
// `Pending` -- nothing in the frame loop has to ask whether it is there yet, and the
// model simply appears once its mesh has loaded. Every animation setting is made before
// the mesh has arrived and is kept until it does.
//
//   ESC  quit
//
// The class is `ModelDemo` because a module named `Model` would shadow `wgr.Model`.
import wgr.*;

@:expose("WgrGuest")
class ModelDemo {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 700;
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";

	static inline final ORBIT_SPEED = 0.4;
	static inline final ORBIT_RADIUS = 9.0;

	static var background:Color;
	static var scene:Scene;
	static var camera:Camera3D;
	static var model:Model;
	static var target:Vec3;
	static var orbitCamera = true;
	static var spinModel = false;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt)); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "model (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(30, 32, 40, 255);
		target = new Vec3(0, 3, 0);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(8, 8, 8), target);
		scene = new Scene();
		scene.setActiveCamera(camera);

		// A scene starts unlit: it needs a sun and some ambient before anything shows.
		final sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.6, -1.0, -0.5));
		sun.setIntensity(3.0); // about pi: a white surface facing it shows its full colour
		scene.add(sun);
		scene.setAmbient(Color.WHITE, 0.3);
		Debug.enableFps(12, 10, 16);

		model = createModel(CHARACTER_PATH);
		scene.add(model);
	}

	static function createModel(meshPath:String):Model {
		final mesh = new Mesh(meshPath); // loads on create
		final model = new Model(mesh); // drawn once its mesh has loaded
		mesh.release(); // the model holds its own reference to the mesh
		model.setPosition(0, 0, 0);
		model.setTint(Color.RAYWHITE);
		// Skeletal animation, if the glTF has any; kept until the mesh arrives.
		model.setAnimation(3);
		model.setAnimationSpeed(1.0);
		model.setAnimationLoop(true);
		return model;
	}

	static function onFrame(dt:Float):Void {
		final t = Wgr.getTime();

		if (orbitCamera)
			camera.setView(new Vec3(Math.cos(t * ORBIT_SPEED) * ORBIT_RADIUS, 7.0,
				Math.sin(t * ORBIT_SPEED) * ORBIT_RADIUS), target);
		if (spinModel)
			model.setTransform(new Vec3(0, 0, 0), new Vec3(0, t * 0.5, 0), Vec3.one());
		model.animate(dt);

		Render.beginFrame();
		Render.clearBackground(background);

		Render.beginMode3D();
		Shape3D.drawGrid(20, 1.0, Color.DARKGRAY);
		Render.endMode3D();

		scene.draw();

		Text.draw("wgrender + sokol — model (glTF/cgltf)", 12, 36, 22, Color.RAYWHITE);
		Text.draw(model.getMesh().getStatus() == Ready ? CHARACTER_PATH + " — skeletal animation (glTF skin)" : "loading model...", 12, 68, 16,
			Color.LIGHTGRAY);
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.getKeyboardState().isPressed(Escape))
			Wgr.requestQuit();
	}
}
