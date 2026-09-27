// wgrender's custom shaders example, as a Haxe guest: materials drawn by your own.
//
// A port of examples/shaders.c. Four shaders, each showing a different thing a custom
// one can reach:
//
//   toon        lights in flat bands with a rim light, on the *animated* character --
//               a custom shader works on a skinned model like any other
//   dissolve    a sphere eaten away and coming back through a noise texture, with a
//               glowing edge: time, a texture of its own, and discard
//   wave        a sphere of water rippling, because a vertex hook moves the surface,
//               reflecting the scene's environment
//   sprite_fx   one material outlining and flashing a logo, used by a 3D sprite in
//               the world *and* a 2D one in the corner -- its shader reads whatever
//               texture the sprite carries
//
// The `.wgrshader` files are compiled from examples/shaders/*.glsl for every backend
// by wgrender's tools/shaderpack.py (`tools/gen_shaders.py --examples`) and ship in its assets,
// so they load through the asset system like any other file. This example was parked
// on the assumption they needed generating first; they did not.
//
//   1 2  toggle the sun and the point light
//   ESC  quit
import wgr.*;

@:expose("WgrGuest")
class Shaders {
	static inline final SCREEN_WIDTH = 960;
	static inline final SCREEN_HEIGHT = 540;

	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";
	static inline final NOISE_PATH = "textures/noise.png";
	static inline final LOGO_PATH = "sprites/logo/wg-logo-white-alpha.png";
	static inline final ENVIRONMENT_PATH = "environments/venice_sunset_1k.hdr";

	static final SHADER_PATHS = [
		"shaders/toon.wgrshader", "shaders/dissolve.wgrshader",
		"shaders/wave.wgrshader", "shaders/sprite_fx.wgrshader"
	];
	// ids 1..4 are the shaders, in that order; then the rest
	static inline final ASSET_TOON = 1;
	static inline final ASSET_DISSOLVE = 2;
	static inline final ASSET_WAVE = 3;
	static inline final ASSET_SPRITE_FX = 4;
	static inline final ASSET_LOGO = 5;
	static inline final ASSET_ENVIRONMENT = 6;
	static inline final ASSET_CHARACTER = 7;
	static inline final ASSET_NOISE = 8;

	static inline final FLOOR_Y = -0.3;
	static inline final SPHERE_Y = FLOOR_Y + 0.5; // spheres 1 m across, resting on the floor
	/** Slot 1 is the character's body; slot 0 is its blob shadow, left alone. **/
	static inline final CHARACTER_BODY_SLOT = 1;

	static var background:Color;
	static var scene:Scene;
	static var camera:Camera3D;
	static var character:Model;
	static var dissolving:Model;
	static var rippling:Model;
	static var dissolve:Material; // borrowed: the model holds the reference
	static var sun:Light;
	static var lamp:Light;
	static var lampMarker:Shape3D;
	static var logo3d:Sprite3D;
	static var logo2d:Sprite2D;
	static var elapsed = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "shaders (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(20, 22, 28, 255);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 1.2, 5.0), new Vec3(0, 0.3, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setAmbient(Color.WHITE, 0.15);

		addLights();
		addModels();
		addSprites();

		for (i in 0...SHADER_PATHS.length)
			load(SHADER_PATHS[i], i + 1);
		load(LOGO_PATH, ASSET_LOGO);
		load(ENVIRONMENT_PATH, ASSET_ENVIRONMENT);
		load(CHARACTER_PATH, ASSET_CHARACTER);
	}

	static function load(path:String, id:Int):Void {
		if (!GuestAbi.loadAsset(path, id))
			Log.error('failed to queue asset: $path');
	}

	static function addLights():Void {
		sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.4, -0.7, -0.6));
		sun.setColor(Color.rgba(255, 244, 228, 255));
		sun.setIntensity(1.5); // soft: the point light and the environment show too
		scene.add(sun);

		lamp = new Light(Point);
		lamp.setColor(Color.rgba(120, 190, 255, 255));
		lamp.setIntensity(9.0); // falls off with distance squared: about 2.3 at 2 m
		lamp.setRange(8.0);
		scene.add(lamp);

		lampMarker = new Shape3D();
		lampMarker.setSphere(0.05);
		lampMarker.setColor(Color.SKYBLUE);
		scene.add(lampMarker);
	}

	static function addModels():Void {
		// Generated meshes: a floor to stand on, and the spheres. The water's is finely
		// divided, because its waves move actual vertices.
		final plane = Mesh.plane(6.0, 6.0, 0);
		final sphere = Mesh.sphere(0.5, 32, 64);
		final fineSphere = Mesh.sphere(0.5, 96, 192);

		final floor = new Model(plane);
		floor.setPosition(0, FLOOR_Y, 0);
		final ground = new Material(Pbr);
		ground.setBaseColor(0.04, 0.04, 0.045, 1.0); // dark, so the lights show on it
		ground.setMetallic(0.0);
		ground.setRoughness(0.8);
		floor.setMaterial(-1, ground);
		ground.release();
		scene.add(floor);

		character = new Model(Handle.NONE); // the mesh attaches when it loads
		character.setTransform(new Vec3(-1.9, FLOOR_Y, 0), new Vec3(0, 0.4, 0),
			new Vec3(0.5, 0.5, 0.5)); // feet at its origin
		character.setAnimation(3);
		scene.add(character);

		dissolving = new Model(sphere);
		dissolving.setPosition(0, SPHERE_Y, 0);
		scene.add(dissolving);

		rippling = new Model(fineSphere);
		rippling.setPosition(1.9, SPHERE_Y, 0);
		scene.add(rippling);

		plane.release(); // the models hold their own references
		sphere.release();
		fineSphere.release();
	}

	/** The logo twice: in the world above the middle, and in the screen's corner. **/
	static function addSprites():Void {
		logo3d = new Sprite3D(Handle.NONE);
		logo3d.setPosition(0, 1.55, -0.8);
		logo3d.setSize(0.9);
		logo3d.setTint(Color.rgba(90, 190, 255, 255)); // so the white flash shows
		scene.add(logo3d);

		logo2d = new Sprite2D(Handle.NONE);
		logo2d.setSize(96.0, 96.0);
		logo2d.setPivot(1.0, 1.0);
		logo2d.setTint(Color.rgba(90, 190, 255, 255));
	}

	/** A shader has loaded: make its material and give it to whatever wears it. **/
	static function onShader(which:Int, path:String):Void {
		final shader = new Shader(path);
		final material = Material.custom(shader);
		shader.release(); // the material holds its own reference
		if (material.isNone())
			return;

		switch which {
			case ASSET_TOON:
				material.setColor("color", Color.rgba(255, 196, 120, 255));
				material.setFloat("bands", 3.0);
				material.setFloat("rim", 0.35);
				character.setMaterial(CHARACTER_BODY_SLOT, material);

			case ASSET_DISSOLVE:
				material.setVec4("color", 0.55, 0.6, 0.7, 1.0); // the shader's own colour, linear
				material.setVec3("edge_color", 4.0, 1.2, 0.2);
				material.setFloat("speed", 0.15);
				material.setDoubleSided(true); // the inside shows through the holes
				dissolving.setMaterial(0, material);
				dissolve = material; // the model holds a reference; ours goes below
				load(NOISE_PATH, ASSET_NOISE);

			case ASSET_WAVE:
				material.setFloat("amplitude", 0.03);
				material.setFloat("frequency", 2.5);
				material.setFloat("wave_speed", 3.0);
				material.setVec4("low_color", 0.0, 0.03, 0.1, 1.0); // deep water
				material.setVec4("high_color", 0.05, 0.3, 0.35, 1.0);
				material.setFloat("roughness", 0.05);
				material.setFloat("reflectivity", 0.35); // real water is 0.02; more, so it shows
				rippling.setMaterial(0, material);

			case ASSET_SPRITE_FX: // one material, a 3D sprite and a 2D one
				material.setVec4("outline_color", 1.0, 0.45, 0.1, 1.0);
				material.setFloat("outline_width", 2.5);
				material.setFloat("flash", 0.8);
				material.setFloat("pulse_speed", 5.0);
				logo3d.setMaterial(material);
				logo2d.setMaterial(material);
		}
		material.release(); // whatever wears it holds its own reference
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		switch id {
			case ASSET_TOON | ASSET_DISSOLVE | ASSET_WAVE | ASSET_SPRITE_FX:
				onShader(id, path);

			case ASSET_LOGO:
				final texture = new Texture(path);
				logo3d.setTexture(texture);
				logo2d.setTexture(texture);
				texture.release(); // the sprites hold their own references

			case ASSET_ENVIRONMENT:
				final environment = new Environment(path);
				// lighting only: the background stays dark
				scene.setEnvironment(environment, 1.0, 0.0);
				environment.release(); // the scene holds its own reference

			case ASSET_CHARACTER:
				final mesh = new Mesh(path);
				character.setMesh(mesh);
				mesh.release();

			case ASSET_NOISE:
				final texture = new Texture(path);
				if (!dissolve.isNone())
					dissolve.setTexture("noise_tex", texture);
				texture.release(); // the material holds its own reference
		}
	}

	static function onFrame(dt:Float):Void {
		if (Input.isKeyPressed(Escape))
			Wgr.requestQuit();
		if (Input.isKeyPressed(Digit1))
			sun.setEnabled(!sun.isEnabled());
		if (Input.isKeyPressed(Digit2))
			lamp.setEnabled(!lamp.isEnabled());

		elapsed += dt;
		// circling in front of the models and facing the camera: always in view, and
		// 1.8 m or more from them -- closer, it would wash them out
		final lx = Math.cos(elapsed * 0.7) * 2.2;
		final ly = 0.8 + Math.sin(elapsed * 0.7) * 1.0;
		final lz = 1.8;
		lamp.setPosition(lx, ly, lz);
		lampMarker.setPosition(lx, ly, lz);
		lampMarker.setVisible(lamp.isEnabled());
		dissolving.setTransform(new Vec3(0, SPHERE_Y, 0), new Vec3(0, elapsed * 0.4, 0), Vec3.ONE);
		character.animate(dt);

		Render.beginFrame();
		Render.clearBackground(background);
		scene.draw();
		final screen = Window.getScreenSize();
		logo2d.setPosition(screen.x - 16.0, screen.y - 16.0); // bottom right
		logo2d.draw();
		Text.draw("wgrender custom shaders: toon, dissolve, water, sprite effects", 12, 12, 20, Color.RAYWHITE);
		Text.draw("1 sun, 2 point light, ESC quit", 12, 40, 16, Color.LIGHTGRAY);
		Render.endFrame();
	}
}
