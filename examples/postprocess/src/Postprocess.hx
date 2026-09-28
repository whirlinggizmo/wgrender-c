// wgrender's postprocess example, as a Haxe guest: screen effects over the frame.
//
// A port of examples/postprocess.c. Two effects, each a custom shader wrapped in a
// material and handed to `Render.addEffect`, applied to the finished frame in the
// order they were added:
//
//   vignette    darkens the edges, with a strength you can change every frame
//   scanlines   a CRT-ish overlay
//
// The point is that an effect is just a `Material`: its parameters are set the same
// way a model's are, so `strength` can breathe with a sine wave and nothing special
// has to happen. `Render.clearEffects` and re-adding is how the chain is rebuilt.
//
//   1 2      toggle each effect
//   UP/DOWN  vignette strength
//   SPACE    make the strength breathe
//   O        stop the camera
//   ESC      quit
//
// The shaders are `.wgrshader` files, compiled from examples/shaders/*.glsl by
// wgrender's tools/shaderpack.py (`tools/gen_shaders.py --examples`) and shipped in its assets.
import wgr.*;

@:expose("WgrGuest")
class Postprocess {
	static inline final SCREEN_WIDTH = 1000;
	static inline final SCREEN_HEIGHT = 640;
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";
	static inline final VIGNETTE_PATH = "shaders/vignette.wgrshader";
	static inline final SCANLINES_PATH = "shaders/scanlines.wgrshader";

	static inline final ASSET_CHARACTER = 1;
	static inline final ASSET_VIGNETTE = 2;
	static inline final ASSET_SCANLINES = 3;

	static var scene:Scene;
	static var camera:Camera3D;
	static var target:Vec3;
	static var lamp:Light;
	static var lampMarker:Shape3D;
	static var character:Model;
	static var torus:Model;
	static var vignette:Material;
	static var scanlines:Material;

	static var vignetteOn = true;
	static var scanlinesOn = true;
	static var breathing = true;
	static var orbit = true;
	static var strength = 0.75;
	static var angle = 0.0;
	static var elapsed = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "postprocess (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		target = new Vec3(0, 1.0, 0);
		camera = new Camera3D(Perspective);
		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setAmbient(Color.rgba(90, 110, 160, 255), 0.12);

		final sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.4, -1.0, -0.5));
		sun.setColor(Color.rgba(255, 215, 170, 255));
		sun.setIntensity(2.2);
		scene.add(sun);

		lamp = new Light(Point);
		lamp.setColor(Color.rgba(80, 220, 255, 255));
		lamp.setIntensity(18.0);
		lamp.setRange(6.0);
		scene.add(lamp);
		lampMarker = new Shape3D();
		lampMarker.setSphere(0.1);
		lampMarker.setColor(Color.rgba(80, 220, 255, 255));
		scene.add(lampMarker);

		addScenery();

		character = new Model(Handle.NONE);
		character.setPosition(0, 0, 0);
		scene.add(character);

		load(CHARACTER_PATH, ASSET_CHARACTER);
		load(VIGNETTE_PATH, ASSET_VIGNETTE);
		load(SCANLINES_PATH, ASSET_SCANLINES);
		Debug.enableFps(12, 10, 16);
	}

	static function load(path:String, id:Int):Void {
		if (!GuestAbi.loadAsset(path, id))
			Log.error('failed to queue asset: $path');
	}

	static function addScenery():Void {
		final plane = Mesh.plane(16.0, 16.0, 0);
		final floor = new Model(plane);
		plane.release();
		final ground = new Material(Pbr);
		ground.setBaseColor(0.07, 0.07, 0.08, 1.0);
		ground.setRoughness(0.85);
		floor.setMaterial(-1, ground);
		ground.release();
		scene.add(floor);

		final shapes = [
			{mesh: Mesh.sphere(0.5, 24, 48), x: -2.2, y: 0.5, r: 0.2, g: 0.55, b: 0.9},
			{mesh: Mesh.torus(0.45, 0.16, 48, 24), x: 2.2, y: 0.7, r: 0.95, g: 0.6, b: 0.25},
			{mesh: Mesh.cube(0.8, 0.8, 0.8), x: 3.6, y: 0.4, r: 0.35, g: 0.85, b: 0.5}
		];
		for (i in 0...shapes.length) {
			final s = shapes[i];
			final model = new Model(s.mesh);
			s.mesh.release();
			model.setPosition(s.x, s.y, -0.6);
			final material = new Material(Pbr);
			material.setBaseColor(s.r, s.g, s.b, 1.0);
			material.setRoughness(0.4);
			model.setMaterial(0, material);
			material.release();
			scene.add(model);
			if (i == 1)
				torus = model; // the one that tumbles
		}
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		switch id {
			case ASSET_CHARACTER:
				final mesh = new Mesh(path);
				character.setMesh(mesh);
				mesh.release();
				character.setAnimation(3);
				character.setAnimationLoop(true);

			case ASSET_VIGNETTE:
				final shader = new Shader(path);
				vignette = Material.custom(shader);
				shader.release(); // the material holds its own reference
				vignette.setFloat("strength", strength);
				vignette.setFloat("radius", 0.25);
				vignette.setVec4("tint", 1.04, 1.0, 0.94, 1.0);
				rebuildEffects();

			case ASSET_SCANLINES:
				final shader = new Shader(path);
				scanlines = Material.custom(shader);
				shader.release();
				scanlines.setFloat("lines", 220.0);
				scanlines.setFloat("darkness", 0.35);
				scanlines.setFloat("offset", 1.5);
				scanlines.setFloat("flicker", 1.0);
				rebuildEffects();
		}
	}

	/** The chain is rebuilt whole rather than edited: clear, then add what is on. **/
	static function rebuildEffects():Void {
		Render.clearEffects();
		if (vignetteOn && !vignette.isNone())
			Render.addEffect(vignette);
		if (scanlinesOn && !scanlines.isNone())
			Render.addEffect(scanlines);
	}

	static function handleKeys(dt:Float):Void {
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
		if (Input.isKeyPressed(Digit1)) {
			vignetteOn = !vignetteOn;
			rebuildEffects();
		}
		if (Input.isKeyPressed(Digit2)) {
			scanlinesOn = !scanlinesOn;
			rebuildEffects();
		}
		if (Input.isKeyPressed(Space))
			breathing = !breathing;
		if (Input.isKeyPressed(O))
			orbit = !orbit;
		if (Input.isKeyDown(Up))
			strength = Math.min(strength + dt, 1.0);
		if (Input.isKeyDown(Down))
			strength = Math.max(strength - dt, 0.0);
	}

	static function onFrame(dt:Float):Void {
		handleKeys(dt);

		elapsed += dt;
		character.animate(dt);
		if (orbit)
			angle += dt * 0.25;
		camera.setView(new Vec3(9.0 * Math.sin(angle), 3.2, 9.0 * Math.cos(angle)), target);

		final lampX = 3.0 * Math.sin(elapsed * 0.9);
		final lampZ = 2.2 + 1.2 * Math.cos(elapsed * 0.9);
		lamp.setPosition(lampX, 1.4, lampZ);
		lampMarker.setPosition(lampX, 1.4, lampZ);
		// isNone, not != null: Model is an abstract over Int, so on a static target
		// there is no null to compare against.
		if (!torus.isNone())
			torus.setTransform(new Vec3(2.2, 0.7, -0.6), new Vec3(0, elapsed * 40.0, elapsed * 25.0), Vec3.ONE);

		// An effect's parameters are its material's: change them any frame you like.
		final live = breathing ? strength * (0.55 + 0.45 * Math.sin(elapsed * 0.8)) : strength;
		if (!vignette.isNone())
			vignette.setFloat("strength", live);

		Render.beginFrame();
		Render.clearBackground(Color.rgba(16, 18, 24, 255));
		scene.draw();
		Text.draw("wgrender post-processing: screen effects over the finished frame", 12, 36, 20, Color.RAYWHITE);
		Text.draw('[1] vignette ${vignetteOn ? "on" : "off"}   [2] scanlines ${scanlinesOn ? "on" : "off"}   '
			+ 'effects: ${Render.effectCount()}', 12, 64, 16, Color.LIGHTGRAY);
		Text.draw('UP/DOWN strength ${fixed(strength, 2)}   SPACE ${breathing ? "stop breathing" : "breathe"}   '
			+ "O camera" + (Wgr.getPlatform() == "web" ? "" : "   ESC quit"), 12, 86, 16, Color.GRAY);
		Render.endFrame();
	}

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
