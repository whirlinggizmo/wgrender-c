// wgrender's sprite2d example, as a Haxe guest: screen-space sprites over a 3D scene.
//
// A port of examples/sprite2d.c. Five sprites off one texture, each showing one thing
// a Sprite2D can do:
//
//   sheet   the 256x256 logo as a 2x2 sheet; setSource steps the four quadrants
//   spin    rotates about its centre, which is the default pivot
//   swing   rotates about its top-left corner, pivot (0, 0)
//   flip    mirrored with a negative x scale
//   tint    a white copy cycling colours -- tint multiplies, so it cannot show on
//           the black logo
//
// Plus a one-off `Texture.draw` in the corner, which draws without an object at all.
// Sprites are scene members, so they draw over the 3D model and are picked before it;
// hovering enlarges whatever is under the pointer, and the alpha test lets the pointer
// through the logo's transparent parts.
//
//   ESC  quit
//
// The class is `Sprite2DDemo` because a module named `Sprite2D` would shadow
// `wgr.Sprite2D` inside itself.
import wgr.*;

@:expose("WgrGuest")
class Sprite2DDemo {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 520;

	static inline final LOGO_PATH = "sprites/logo/wg-logo-bw-alpha.png";
	static inline final WHITE_LOGO_PATH = "sprites/logo/wg-logo-white-alpha.png";
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";

	static inline final ASSET_LOGO = 1;
	static inline final ASSET_WHITE_LOGO = 2;
	static inline final ASSET_MESH = 3;

	static inline final SPRITE_COUNT = 5;
	static inline final TINT_SPRITE = 4;
	static inline final FLIP_SPRITE = 3;
	static inline final PALETTE_SIZE = 24;
	static inline final SHEET_CELL = 128.0;

	static final NAMES = ["sheet", "spin", "swing", "flip", "tint"];

	static var background:Color;
	static var scene:Scene;
	static var camera:Camera3D;
	static var model:Model;
	static var logo:Texture;
	static var sprites:Array<Sprite2D> = [];
	static var palette:Array<Color> = [];
	static var elapsed = 0.0;
	static var sheetFrame = 0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "sprite2d (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(28, 30, 38, 255);
		// A colour is immutable, so the cycling tint walks a palette built up front
		// rather than being recoloured each frame.
		for (i in 0...PALETTE_SIZE) {
			final a = i / PALETTE_SIZE * 2 * Math.PI;
			palette.push(Color.rgba(Std.int(127 + 127 * Math.sin(a)), Std.int(127 + 127 * Math.sin(a + 2.1)),
				Std.int(127 + 127 * Math.sin(a + 4.2)), 255));
		}

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 1.4, 5.5), new Vec3(0, 1, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);

		final sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.5, -1.0, -0.7));
		sun.setIntensity(3.0);
		scene.add(sun);
		scene.setAmbient(Color.WHITE, 0.35);

		model = new Model(Handle.NONE); // the mesh is attached when it loads
		model.setAnimation(3);
		model.setAnimationLoop(true);
		scene.add(model);

		for (i in 0...SPRITE_COUNT) {
			final sprite = new Sprite2D(Handle.NONE); // the texture likewise
			sprite.setSize(128, 128);
			sprite.setPickAlphaTest(true, 0.5);
			scene.add(sprite);
			sprites.push(sprite);
		}
		sprites[0].setPosition(140, 170);
		sprites[1].setPosition(140, 380);
		sprites[2].setPivot(0, 0);
		sprites[2].setPosition(700, 110);
		sprites[2].setSize(96, 96);
		sprites[FLIP_SPRITE].setPosition(760, 400);
		sprites[FLIP_SPRITE].setScale(-1, 1);
		sprites[TINT_SPRITE].setPosition(450, 110);
		sprites[TINT_SPRITE].setSize(96, 96);

		load(LOGO_PATH, ASSET_LOGO);
		load(WHITE_LOGO_PATH, ASSET_WHITE_LOGO);
		load(CHARACTER_PATH, ASSET_MESH);
	}

	static function load(path:String, id:Int):Void {
		if (!GuestAbi.loadAsset(path, id))
			Log.error('failed to queue asset: $path');
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		switch id {
			case ASSET_LOGO:
				// Kept, not released: the corner Texture.draw below needs the handle,
				// so this one outlives the sprites that also reference it.
				logo = new Texture(path);
				for (i in 0...SPRITE_COUNT)
					if (i != TINT_SPRITE)
						sprites[i].setTexture(logo);

			case ASSET_WHITE_LOGO:
				final texture = new Texture(path);
				sprites[TINT_SPRITE].setTexture(texture);
				texture.release(); // the sprite holds its own reference

			case ASSET_MESH:
				final mesh = new Mesh(path);
				model.setMesh(mesh);
				mesh.release();
		}
	}

	static function animate(dt:Float):Void {
		elapsed += dt;
		model.animate(dt);

		// sprite sheet: one quadrant of the 256x256 texture per frame
		sheetFrame = Std.int(elapsed * 2.0) % 4;
		sprites[0].setSource((sheetFrame % 2) * SHEET_CELL, Std.int(sheetFrame / 2) * SHEET_CELL, SHEET_CELL,
			SHEET_CELL);
		sprites[1].setRotation(elapsed);
		sprites[2].setRotation(Math.sin(elapsed * 1.5) * 0.8);
		sprites[TINT_SPRITE].setTint(palette[Std.int(elapsed * 6.0) % PALETTE_SIZE]);
	}

	/** Hover: 2D sprites are picked before the model behind them. **/
	static function hover(mouse:MouseState):String {
		final pick = scene.pick(mouse.x, mouse.y);
		final hovered = pick.hit ? pick.handle : Handle.NONE;
		var name = "nothing";
		for (i in 0...SPRITE_COUNT) {
			final grow = sprites[i] == hovered ? 1.15 : 1.0;
			// "flip" keeps its mirror while it grows.
			sprites[i].setScale(i == FLIP_SPRITE ? -grow : grow, grow);
			if (sprites[i] == hovered)
				name = NAMES[i];
		}
		return pick.hit && pick.handle == model ? "model" : name;
	}

	static function onFrame(dt:Float):Void {
		final mouse = Input.getMouseState();
		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();

		animate(dt);
		final hovering = hover(mouse);

		Render.beginFrame();
		Render.clearBackground(background);
		scene.draw();
		// one-off, with no object behind it
		logo.draw(Window.getScreenSize().x - 74, 10, 64, 64, Color.WHITE);

		Text.draw("wgrender sprite2d: source rect, pivot, rotation, flip, picking", 12, 12, 16, Color.RAYWHITE);
		Text.draw('mouse (${mouse.x}, ${mouse.y})  hover: $hovering  sheet frame $sheetFrame', 12, 36, 16,
			Color.LIGHTGRAY);
		Render.endFrame();
	}
}
