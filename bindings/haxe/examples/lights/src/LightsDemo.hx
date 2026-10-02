// wgrender's lights example, as a Haxe guest: directional, point and spot.
//
// A port of examples/lights.c. Five animated models on a grid under three lights that
// can each be switched off, so it is clear which one is doing what:
//
//   a dim warm sun         directional, no position, only a direction
//   a cyan lamp            point, orbiting the row and falling off with its range;
//                          the small sphere marks it, and is unlit so it shows the
//                          light's own colour rather than being lit by it
//   a white spotlight      sweeping across from above, a cone in radians
//
// Behind them stand billboards with a built-in material, which is what makes them take
// the scene's lights the way the models do -- a Sprite3D without one is drawn flat. Each
// shows one cell of the tilemap example's sprite sheet, and the sheet's normal map gives
// it relief: the normal map is sampled through the same region.
//
// A scene starts with no lights and no ambient. Everything here is explicit, and
// turning all three off leaves the ambient alone, which is the point of trying it.
//
//   1 2 3  toggle the sun, the lamp, the spotlight
//   ESC    quit
import wgr.*;

@:expose("WgrGuest")
class LightsDemo {
	static inline final SCREEN_WIDTH = 1000;
	static inline final SCREEN_HEIGHT = 600;
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";
	static inline final SPRITE_PATH = "textures/tiles.png";
	static inline final NORMAL_PATH = "textures/tiles_sheet_normal.png"; // wgrender's tools/gen_tile_sheet.py

	static inline final MODEL_COUNT = 5;
	static inline final SPRITE_COUNT = 4;

	// the sprites' cells in the sheet (pixels, from wgrender's tools/gen_tile_sheet.py) and
	// their world height; all 1.6 wide
	static final SPRITE_CELLS = [
		[62.0, 2, 16, 16, 1.6], // stone
		[2.0, 22, 16, 32, 3.2], // tree
		[42.0, 22, 16, 16, 1.6], // coin
		[62.0, 22, 16, 16, 1.6], // rock
	];

	static var background:Color;
	static var gridColor:Color;
	static var lampColor:Color;

	static var scene:Scene;
	static var camera:Camera3D;
	static var models:Array<Model> = [];
	static var sprites:Array<Sprite3D> = [];
	static var spriteMaterial:Material;
	static var sun:Light;
	static var lamp:Light;
	static var lampMarker:Shape3D;
	static var spot:Light;
	static var elapsed = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {}); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "lights (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(12, 13, 18, 255);
		gridColor = Color.rgba(40, 42, 50, 255);
		lampColor = Color.rgba(60, 220, 255, 255);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 4.5, 10), new Vec3(0, 1, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setAmbient(Color.rgba(90, 110, 160, 255), 0.05);

		addModels();
		addLights();
		addSprites();

		makeMesh(); // loads on create: drawn once it has loaded
	}

	static function addModels():Void {
		for (i in 0...MODEL_COUNT) {
			final model = new Model(Handle.NONE); // makeMesh gives them all one mesh
			model.setPosition(-4.0 + 2.0 * i, 0, i % 2 == 1 ? -0.8 : 0.8);
			model.setAnimation(3);
			model.setAnimationLoop(true);
			scene.add(model);
			models.push(model);
		}
	}

	static function addLights():Void {
		sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.4, -1.0, -0.6));
		sun.setColor(Color.rgba(255, 210, 160, 255));
		sun.setIntensity(1.1);
		scene.add(sun);

		lamp = new Light(Point);
		lamp.setColor(lampColor);
		lamp.setIntensity(20.0);
		lamp.setRange(5.0);
		scene.add(lamp);

		// Unlit, so it shows the lamp's colour rather than being lit by it.
		lampMarker = new Shape3D();
		lampMarker.setSphere(0.12);
		lampMarker.setColor(lampColor);
		scene.add(lampMarker);

		spot = new Light(Spot);
		spot.setPosition(0, 6, 2);
		spot.setSpotCone(0.14, 0.28); // radians: about 8 and 16 degrees
		spot.setIntensity(125.0);
		scene.add(spot);
	}

	/** Lit billboards: a built-in material is what lets the scene's lights reach them. **/
	static function addSprites():Void {
		spriteMaterial = new Material(Pbr);
		spriteMaterial.setMetallic(0.0);
		spriteMaterial.setRoughness(0.55);
		// cells sit side by side in the sheet: clamp, so none reaches into the next
		spriteMaterial.setTextureSampling("normal_texture", Clamp, Clamp, Linear);
		final normalMap = new Texture(NORMAL_PATH);
		spriteMaterial.setNormalTexture(normalMap);
		normalMap.release(); // the material holds its own reference
		final sheet = new Texture(SPRITE_PATH); // the sprites appear once it's loaded
		sheet.setSampling(Clamp, Clamp, Nearest);
		for (i in 0...SPRITE_COUNT) {
			final cell = SPRITE_CELLS[i];
			final sprite = new Sprite3D(sheet);
			sprite.setPosition(-3.0 + 2.0 * i, 0.2, -2.5);
			sprite.setSource(cell[0], cell[1], cell[2], cell[3]);
			sprite.setExtent(1.6, cell[4]);
			sprite.setPivot(0.5, 1.0); // standing on their bottom edge
			sprite.setAlphaMode(Mask, 0.5);
			sprite.setMaterial(spriteMaterial);
			scene.add(sprite);
			sprites.push(sprite);
		}
		spriteMaterial.release(); // the sprites hold it
		sheet.release(); // and the sheet
	}

	static function makeMesh():Void {
		final mesh = new Mesh(CHARACTER_PATH);
		for (model in models)
			model.setMesh(mesh);
		mesh.release(); // the models hold their own references
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		if (keys.isPressed(Digit1))
			sun.setEnabled(!sun.isEnabled());
		if (keys.isPressed(Digit2))
			lamp.setEnabled(!lamp.isEnabled());
		if (keys.isPressed(Digit3))
			spot.setEnabled(!spot.isEnabled());
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();

		elapsed += dt;
		for (model in models)
			model.animate(dt);

		// the lamp orbits through the row; the spotlight sweeps left and right
		final lx = Math.sin(elapsed * 0.6) * 5.0;
		final lz = Math.cos(elapsed * 0.6) * 2.0;
		lamp.setPosition(lx, 1.2, lz);
		lampMarker.setPosition(lx, 1.2, lz);
		lampMarker.setVisible(lamp.isEnabled());
		spot.setDirection(new Vec3(Math.sin(elapsed * 0.8) * 0.7, -1.0, -0.3));

		Render.beginFrame();
		Render.clearBackground(background);
		Render.beginMode3D();
		Shape3D.drawGrid(20, 1.0, gridColor);
		Render.endMode3D();
		scene.draw();

		Text.draw("wgrender lights: directional, point, spot", 12, 12, 20, Color.RAYWHITE);
		Text.draw('[1] sun ${on(sun)}   [2] point light ${on(lamp)}   [3] spotlight ${on(spot)}', 12, 40, 16,
			Color.LIGHTGRAY);
		Text.drawFps(12, 64);
		Render.endFrame();
	}

	static inline function on(light:Light):String
		return light.isEnabled() ? "on " : "off";
}
