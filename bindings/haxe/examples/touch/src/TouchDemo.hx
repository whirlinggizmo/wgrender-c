// wgrender's touch example, as a Haxe guest: fingers and the two-finger gesture.
//
//   - every finger gets a numbered ring (its id) while it's down, and a fading one
//     where it lifted;
//   - two fingers pan, pinch and twist the logo (`Input.getTouchGesture`), about the
//     point between them, so it stays under your fingers;
//   - one finger is also the pointer: drag the coin. A second finger cancels that
//     drag (the pointer is released off-screen), so a pinch never drops the coin
//     somewhere or clicks anything.
//
// Without a touch screen: drag with the mouse, and the wheel zooms the logo. ESC quits.
//
// The C hangs a per-file callback off `wgr_asset_add_task`, which is one of the calls
// the guest ABI replaces — the asset op reports every file by the id the load was
// given, so the ids here stand in for those two callbacks.
import wgr.*;

@:expose("WgrGuest")
class TouchDemo {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 700;
	static inline final LOGO_PATH = "sprites/logo/wg-logo-white-alpha.png";
	static inline final TILES_PATH = "textures/tiles.png";
	static inline final LOGO_ID = 1;
	static inline final TILES_ID = 2;
	static inline final RING = 38.0;

	static var logo:Sprite2D;
	static var tile:Sprite2D;
	static var logoX = 0.0;
	static var logoY = 0.0;
	static var logoScale = 1.0;
	static var logoRotation = 0.0;
	static var tileX = 0.0;
	static var tileY = 0.0;
	static var dragging = false;

	/** x, y and a fade from 1 to 0, per finger id. **/
	static var lifted:Array<Array<Float>> = [];

	static var colors:Array<Color> = [];

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "touch (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		final screen = Window.getScreenSize();
		logo = Handle.NONE;
		tile = Handle.NONE;
		logoX = screen.x * 0.5;
		logoY = screen.y * 0.45;
		tileX = screen.x * 0.5;
		tileY = screen.y * 0.8;
		for (i in 0...Input.MAX_TOUCHES) {
			lifted.push([0.0, 0.0, 0.0]);
			colors.push(Color.rgba(90 + 20 * i, 200 - 15 * i, 120 + 17 * i, 255));
		}
		GuestAbi.loadAsset(LOGO_PATH, LOGO_ID);
		GuestAbi.loadAsset(TILES_PATH, TILES_ID);
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		switch (id) {
			case LOGO_ID:
				logo = new Sprite2D(new Texture(path));
				logo.setSize(240, 240);
			case TILES_ID:
				final texture = new Texture(path);
				texture.setSampling(Clamp, Clamp, Nearest);
				tile = new Sprite2D(texture);
				tile.setSource(32, 16, 16, 16); // the coin
				tile.setSize(96, 96);
		}
	}

	/** Scale and turn the logo about (x, y), so the point under the fingers stays put. **/
	static function transformLogo(x:Float, y:Float, scale:Float, rotation:Float):Void {
		final c = Math.cos(rotation), s = Math.sin(rotation);
		final ox = (logoX - x) * scale, oy = (logoY - y) * scale;
		logoX = x + ox * c - oy * s;
		logoY = y + ox * s + oy * c;
		logoScale = Math.min(Math.max(logoScale * scale, 0.2), 8.0);
		logoRotation += rotation;
	}

	static function onFrame(dt:Float):Void {
		final mouse = Input.getMouseState();
		final gesture = Input.getTouchGesture();
		final count = Input.getTouchCount();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.getKey(Escape) == ButtonState.Pressed)
			Wgr.requestQuit();

		// two fingers move the logo; the wheel zooms it about the mouse
		if (gesture.active) {
			logoX += gesture.dx;
			logoY += gesture.dy;
			transformLogo(gesture.x, gesture.y, gesture.scale, gesture.rotation);
		}
		if (mouse.wheel != 0.0)
			transformLogo(mouse.x, mouse.y, Math.pow(1.1, mouse.wheel), 0.0);

		// the pointer (mouse, or one finger) drags the coin
		if (mouse.left == ButtonState.Pressed && Math.abs(mouse.x - tileX) < 48 && Math.abs(mouse.y - tileY) < 48)
			dragging = true;
		else if (mouse.left == ButtonState.Released || mouse.left == ButtonState.Up)
			dragging = false;
		if (dragging) {
			tileX = mouse.x;
			tileY = mouse.y;
		}

		// where fingers lifted: a ring that fades
		for (i in 0...Input.MAX_TOUCHES)
			lifted[i][2] = Math.max(lifted[i][2] - dt * 2.0, 0.0);
		for (i in 0...count) {
			final touch = Input.getTouch(i);
			if (touch.state == ButtonState.Released) {
				lifted[touch.id][0] = touch.x;
				lifted[touch.id][1] = touch.y;
				lifted[touch.id][2] = 1.0;
			}
		}

		Render.beginFrame();
		Render.clearBackground(Color.rgba(22, 25, 33, 255));
		if (!logo.isNone()) {
			logo.setPosition(logoX, logoY);
			logo.setScale(logoScale, logoScale);
			logo.setRotation(logoRotation);
			logo.draw();
		}
		if (!tile.isNone()) {
			tile.setPosition(tileX, tileY);
			tile.setTint(dragging ? Color.rgba(255, 230, 150, 255) : Color.WHITE);
			tile.draw();
		}
		for (i in 0...Input.MAX_TOUCHES) {
			if (lifted[i][2] > 0.0)
				Shape2D.drawCircleLines(new Vec2(lifted[i][0], lifted[i][1]), RING * (2.0 - lifted[i][2]),
					colors[i].withAlpha(Std.int(200 * lifted[i][2])));
		}
		for (i in 0...count) {
			final touch = Input.getTouch(i);
			if (touch.state == ButtonState.Released)
				continue;
			final at = new Vec2(touch.x, touch.y);
			Shape2D.drawCircle(at, RING, colors[touch.id].withAlpha(90));
			Shape2D.drawCircleLines(at, RING, colors[touch.id]);
			Text.draw(Std.string(touch.id), Std.int(touch.x) - 6, Std.int(touch.y - RING) - 26, 22, colors[touch.id]);
		}
		if (gesture.active)
			Shape2D.drawCircle(new Vec2(gesture.x, gesture.y), 6, Color.WHITE);

		final down = mouse.left == ButtonState.Down || mouse.left == ButtonState.Pressed;
		Text.draw('fingers: $count   pointer: ${down ? "down" : "up"}', 16, 16, 18, Color.WHITE);
		Text.draw('logo: scale ${fixed(logoScale, 2)}, turn ${Math.round(logoRotation * 57.29578)} deg', 16, 40, 18,
			Color.WHITE);
		Text.draw("two fingers: pan, pinch, twist the logo; one finger drags the coin", 16, 64, 16,
			Color.rgba(150, 158, 175, 255));
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
