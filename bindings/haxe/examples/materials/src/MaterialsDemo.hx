// wgrender's materials example, as a Haxe guest: glTF metallic-roughness, in code.
//
// A port of examples/materials.c.
//
//   top row      dielectric spheres (metallic 0), roughness 0 to 1 left to right
//   middle row   metal spheres (metallic 1), the same roughness steps
//   bottom row   unlit, emissive, normal mapped, alpha blended, and the character with
//                its body material replaced by gold -- on this model only
//
// Two things in here are the actual subject. One sphere mesh backs every sphere and
// each model overrides the mesh's material, so the material belongs to the instance
// rather than the geometry. And every material is assigned before the mesh has
// finished loading, which works because a model holds the assignment and applies it
// when the mesh arrives.
//
//   1 2  toggle the sun and the lamp
//   ESC  quit
import wgr.*;

@:expose("WgrGuest")
class MaterialsDemo {
	static inline final SCREEN_WIDTH = 1000;
	static inline final SCREEN_HEIGHT = 700;
	static inline final SPHERE_PATH = "models/sphere/sphere.glb";
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";
	static inline final NORMAL_MAP_PATH = "textures/tiles_normal.png";

	static inline final ASSET_SPHERE = 1;
	static inline final ASSET_CHARACTER = 2;
	static inline final ASSET_NORMAL_MAP = 3;

	static inline final COLUMNS = 5;
	static inline final SPACING = 1.35;
	/** Slot 1 is the character's body; slot 0 is its blob shadow, which is left alone. **/
	static inline final CHARACTER_BODY_SLOT = 1;

	static var background:Color;
	static var scene:Scene;
	static var camera:Camera3D;
	static var spheres:Array<Model> = [];
	static var bottomRow:Array<Model> = [];
	static var character:Model;
	static var tiles:Material; // the normal-mapped one; its texture arrives later
	static var sun:Light;
	static var lamp:Light;
	static var lampMarker:Shape3D;
	static var elapsed = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "materials (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(20, 22, 28, 255);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 1.6, 7.5), new Vec3(0, 1.2, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setAmbient(Color.WHITE, 0.12);

		addLights();
		addRoughnessRows();
		addBottomRow();
		addCharacter();

		load(SPHERE_PATH, ASSET_SPHERE);
		load(CHARACTER_PATH, ASSET_CHARACTER);
		load(NORMAL_MAP_PATH, ASSET_NORMAL_MAP);
	}

	static function load(path:String, id:Int):Void {
		if (!GuestAbi.loadAsset(path, id))
			Log.error('failed to queue asset: $path');
	}

	static function addLights():Void {
		sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.4, -0.7, -0.6));
		sun.setColor(Color.rgba(255, 244, 228, 255));
		sun.setIntensity(3.0);
		scene.add(sun);

		lamp = new Light(Point);
		lamp.setColor(Color.rgba(120, 190, 255, 255));
		lamp.setIntensity(8.0);
		lamp.setRange(10.0);
		scene.add(lamp);

		// Shapes are unlit, so this shows the light's own colour.
		lampMarker = new Shape3D();
		lampMarker.setSphere(0.06);
		lampMarker.setColor(Color.SKYBLUE);
		scene.add(lampMarker);
	}

	/** Linear rgb, glTF's factor -- not an sRGB `Color`. **/
	static function pbr(r:Float, g:Float, b:Float, metallic:Float, roughness:Float):Material {
		final material = new Material(Pbr);
		material.setBaseColor(r, g, b, 1.0);
		material.setMetallic(metallic);
		material.setRoughness(roughness);
		return material;
	}

	/** Place a sphere and give it `material`; the model keeps its own reference. **/
	static function sphere(x:Float, y:Float, material:Material):Model {
		final model = new Model(Handle.NONE); // the mesh is attached when it loads
		model.setPosition(x, y, 0);
		model.setMaterial(0, material);
		material.release();
		scene.add(model);
		spheres.push(model);
		return model;
	}

	static function addRoughnessRows():Void {
		for (c in 0...COLUMNS) {
			final x = (c - (COLUMNS - 1) * 0.5) * SPACING;
			final roughness = c / (COLUMNS - 1);
			sphere(x, 2.7, pbr(0.8, 0.05, 0.04, 0.0, roughness)); // red plastic
			sphere(x, 1.35, pbr(1.0, 0.77, 0.34, 1.0, roughness)); // gold
		}
	}

	static function addBottomRow():Void {
		// unlit: ignores the lights entirely
		final unlit = new Material(Unlit);
		unlit.setColor("base_color", Color.SKYBLUE);
		bottomRow.push(sphere(-2 * SPACING, 0.0, unlit));

		// emissive: glows whatever the lighting does
		final emissive = pbr(0.05, 0.05, 0.05, 0.0, 0.6);
		emissive.setEmissive(1.0, 0.35, 0.05);
		bottomRow.push(sphere(-SPACING, 0.0, emissive));

		// normal mapped: bevelled tiles, tangents generated at load
		tiles = pbr(0.6, 0.6, 0.62, 0.0, 0.45);
		tiles.setNormalScale(1.0);
		bottomRow.push(sphere(0.0, 0.0, tiles)); // releases our reference; the model keeps one

		// alpha blended glass
		final glass = pbr(0.3, 0.9, 0.5, 0.0, 0.1);
		glass.setBaseColor(0.3, 0.9, 0.5, 0.35);
		glass.setAlphaMode(Blend, 0.5);
		bottomRow.push(sphere(SPACING, 0.0, glass));
	}

	static function addCharacter():Void {
		character = new Model(Handle.NONE);
		character.setTransform(new Vec3(2 * SPACING, -0.55, 0), new Vec3(0, -0.6, 0), new Vec3(0.3, 0.3, 0.3));
		character.setAnimation(3);
		final gold = pbr(1.0, 0.77, 0.34, 1.0, 0.3);
		character.setMaterial(CHARACTER_BODY_SLOT, gold);
		gold.release();
		scene.add(character);
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		switch id {
			case ASSET_SPHERE:
				final mesh = new Mesh(path);
				for (model in spheres)
					model.setMesh(mesh);
				mesh.release(); // the models hold their own references

			case ASSET_CHARACTER:
				final mesh = new Mesh(path);
				character.setMesh(mesh);
				mesh.release();

			case ASSET_NORMAL_MAP:
				final texture = new Texture(path);
				tiles.setNormalTexture(texture);
				texture.release(); // the material holds its own reference
		}
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();
		if (keys.isPressed(Digit1))
			sun.setEnabled(!sun.isEnabled());
		if (keys.isPressed(Digit2))
			lamp.setEnabled(!lamp.isEnabled());

		elapsed += dt;
		final lx = Math.cos(elapsed * 0.7) * 4.0;
		final ly = 1.4 + Math.sin(elapsed * 0.9) * 1.2;
		final lz = Math.sin(elapsed * 0.7) * 1.5 + 2.0;
		lamp.setPosition(lx, ly, lz);
		lampMarker.setPosition(lx, ly, lz);
		lampMarker.setVisible(lamp.isEnabled());

		// turn the bottom row, so the normal map has something to catch
		for (i in 0...bottomRow.length)
			bottomRow[i].setTransform(new Vec3((i - 2.0) * SPACING, 0.0, 0), new Vec3(0, elapsed * 0.5, 0), Vec3.ONE);
		character.animate(dt);

		Render.beginFrame();
		Render.clearBackground(background);
		scene.draw();
		Text.draw("wgrender materials: metallic-roughness, unlit, emissive, normal map, blend", 12, 12, 16,
			Color.RAYWHITE);
		Text.draw('roughness 0 -> 1 (left to right)   rows: plastic, gold   '
			+ '[1] sun ${sun.isEnabled() ? "on" : "off"}  [2] lamp ${lamp.isEnabled() ? "on" : "off"}', 12, 36, 16,
			Color.LIGHTGRAY);
		Render.endFrame();
	}
}
