// wgrender's tilemap example, as a Haxe guest: a scrolling 2D tile map on an orthographic camera.
//
// A port of examples/tilemap.c, and the example that explains an absence: wgrender has no
// Camera2D. A scrolling, zooming 2D world is `Sprite3D`s in the XY plane with `Free`
// facing under an orthographic `Camera3D`, which keeps the same scenes, layers, depth
// order and ray picking the 3D path has.
//
//   - one sprite sheet, one Sprite3D per cell, cut out with `setSource`
//   - trees and flags are 16x32 in the sheet and drawn 1x2 world units with
//     `setExtent`; their pivot is the bottom edge, so a prop stands on its cell
//     whatever its height
//   - drag or arrow keys scroll, the wheel zooms -- zoom being the camera's
//     orthographic height, so fewer world units visible is closer
//   - the sheet is pixel art, alpha 0 or 1: ground is `Opaque`, props are `Mask`, so
//     nothing needs sorting and each layer draws in one batch
//   - coins answer the pointer through the scene's interaction state rather than a
//     pick of their own. Their picks are alpha-tested, so a coin's transparent corner
//     lets the tile behind it take the click
//
//   ESC  quit
import wgr.*;

@:expose("WgrGuest")
class Tilemap {
	static inline final SCREEN_WIDTH = 960;
	static inline final SCREEN_HEIGHT = 600;
	static inline final TILES_PATH = "textures/tiles.png";
	static inline final ASSET_TILES = 1;

	static inline final WORLD_W = 24;
	static inline final WORLD_H = 16;
	static inline final LAYER_GROUND = 0;
	static inline final LAYER_PROPS = 1;
	static inline final COIN_COUNT = 8;

	static inline final ZOOM_MIN = 4.0;
	static inline final ZOOM_MAX = 32.0;

	// cells of the sheet, in texture pixels: x, y, width, height (wgrender's tools/gen_tile_sheet.py;
	// each cell has a gutter around it that repeats its edge, so sampling never reaches a neighbour)
	static final GRASS = [2.0, 2, 16, 16];
	static final SAND = [22.0, 2, 16, 16];
	static final WATER = [42.0, 2, 16, 16];
	static final STONE = [62.0, 2, 16, 16];
	static final TREE = [2.0, 22, 16, 32];
	static final FLAG = [22.0, 22, 16, 32];
	static final COIN = [42.0, 22, 16, 16];
	static final ROCK = [62.0, 22, 16, 16];

	static var background:Color;
	static var shade:Color;
	static var textColor:Color;
	static var dim:Color;
	static var highlight:Color;

	static var scene:Scene;
	static var camera:Camera3D;
	static var texture:Texture;
	static var coins:Array<Sprite3D> = [];
	static var collected = 0;
	static var centreX = 0.0;
	static var centreY = 0.0;
	static var zoom = 12.0; // world units visible vertically
	static var loaded = false;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		// No MSAA: the tiles are quads meeting edge to edge, and multisampled edges
		// let the background through as a hairline seam between them.
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "tilemap (wgrender host, Haxe guest)", Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(24, 28, 38, 255);
		shade = Color.rgba(18, 20, 28, 190);
		textColor = Color.rgba(235, 238, 245, 255);
		dim = Color.rgba(140, 146, 158, 255);
		highlight = Color.rgba(255, 230, 140, 255);

		centreX = WORLD_W * 0.5;
		centreY = WORLD_H * 0.5;
		camera = new Camera3D(Orthographic);
		placeCamera();

		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setInteractive(true);

		if (!GuestAbi.loadAsset(TILES_PATH, ASSET_TILES))
			Log.error('failed to queue asset: $TILES_PATH');
	}

	static function placeCamera():Void {
		camera.setView(new Vec3(centreX, centreY, 10.0), new Vec3(centreX, centreY, 0.0));
		camera.setOrthoHeight(zoom);
	}

	/** A small hand-made map: water along the bottom, a sand shore, stone paths. **/
	static function tileAt(x:Int, y:Int):Array<Float> {
		if (y < 2)
			return WATER;
		if (y < 3)
			return SAND;
		if (x == 8 || y == 9)
			return STONE;
		return GRASS;
	}

	/**
		One cell of the sheet in the world. Ground tiles sit at z 0 and props just in
		front, so props draw over the ground by depth rather than by draw order.
	**/
	static function addSprite(cell:Array<Float>, x:Float, y:Float, z:Float, width:Float, height:Float, pivotY:Float,
			layer:Int):Sprite3D {
		final sprite = new Sprite3D(texture);
		sprite.setFacing(Free);
		sprite.setSource(cell[0], cell[1], cell[2], cell[3]);
		sprite.setExtent(width, height);
		sprite.setPivot(0.5, pivotY);
		sprite.setPosition(x, y, z);
		sprite.setAlphaMode(layer == LAYER_GROUND ? Opaque : Mask, 0.5);
		scene.add(sprite, layer);
		return sprite;
	}

	static function addProp(cell:Array<Float>, x:Float, y:Float, width:Float, height:Float, coin:Bool):Void {
		// The pivot is the bottom edge, so a prop stands on its cell whatever its height.
		final prop = addSprite(cell, x, y - 0.5, 0.1, width, height, 1.0, LAYER_PROPS);
		if (coin) {
			prop.setPickAlphaTest(true, 0.5);
			coins.push(prop);
		} else {
			prop.setPickable(false);
		}
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		if (id != ASSET_TILES)
			return;
		texture = new Texture(path);
		// pixel art: keep the texels crisp when zoomed in
		texture.setSampling(Clamp, Clamp, Nearest);
		buildWorld();
		loaded = true;
	}

	static function buildWorld():Void {
		for (y in 0...WORLD_H) {
			for (x in 0...WORLD_W) {
				// A hair over one unit: neighbouring quads are blended separately, so
				// an edge landing exactly on a pixel boundary shows the background
				// through as a hairline seam.
				final tile = addSprite(tileAt(x, y), x + 0.5, y + 0.5, 0.0, 1.01, 1.01, 0.5, LAYER_GROUND);
				tile.setPickable(false);
			}
		}
		for (i in 0...7) // trees: 16x32 cells drawn 1x2
			addProp(TREE, 2.5 + i * 3.0, 12.5 - (i % 3), 1.0, 2.0, false);
		addProp(FLAG, 8.5, 9.5, 1.0, 2.0, false);
		for (i in 0...6)
			addProp(ROCK, 4.5 + i * 3.5, 4.5 + (i % 2) * 2.0, 1.0, 1.0, false);
		for (i in 0...COIN_COUNT)
			addProp(COIN, 3.5 + i * 2.5, 7.5 + (i % 3), 0.8, 0.8, true);
	}

	static function scroll(dt:Float, keys:KeyboardState, mouse:MouseState, unitsPerPixel:Float):Void {
		if (mouse.left == ButtonState.Down) {
			centreX -= mouse.dx * unitsPerPixel;
			centreY += mouse.dy * unitsPerPixel; // screen y is down, world y is up
		}
		final speed = zoom * 0.6 * dt;
		if (keys.isDown(Left))
			centreX -= speed;
		if (keys.isDown(Right))
			centreX += speed;
		if (keys.isDown(Down))
			centreY -= speed;
		if (keys.isDown(Up))
			centreY += speed;

		if (mouse.wheel != 0) { // fewer world units visible = closer
			zoom -= mouse.wheel * 1.5;
			zoom = zoom < ZOOM_MIN ? ZOOM_MIN : (zoom > ZOOM_MAX ? ZOOM_MAX : zoom);
		}
		centreX = centreX < 0 ? 0 : (centreX > WORLD_W ? WORLD_W : centreX);
		centreY = centreY < 0 ? 0 : (centreY > WORLD_H ? WORLD_H : centreY);
	}

	/** Hover lights a coin up, a click collects it -- both from the scene, not a pick. **/
	static function updateCoins():Void {
		for (coin in coins) {
			final hover = scene.getHover(coin);
			coin.setTint(hover == Pressed || hover == Down ? highlight : Color.WHITE);
			if (scene.isClicked(coin)) {
				coin.setVisible(false);
				coin.setPickable(false);
				collected++;
			}
		}
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		final mouse = Input.getMouseState();
		final screen = Window.getScreenSize();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();

		scroll(dt, keys, mouse, screen.y > 0 ? zoom / screen.y : 0);
		placeCamera();
		updateCoins();

		Render.beginFrame();
		Render.clearBackground(background);
		scene.draw();
		Render.beginMode2D(); // back to screen space for the HUD
		Shape2D.drawRectangle(0, 0, screen.x, 88, shade);
		Text.draw("wgrender tilemap: an orthographic camera over sprite3d tiles", 20, 20, 20, textColor);
		Text.draw('coins: $collected of $COIN_COUNT   zoom: ${fixed(zoom, 1)} units   '
			+ 'center: ${fixed(centreX, 1)}, ${fixed(centreY, 1)}${loaded ? "" : "   (loading)"}', 20, 46, 15, dim);
		Text.draw("drag or arrows to scroll, wheel to zoom, click the coins", 20, 68, 15, dim);
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
