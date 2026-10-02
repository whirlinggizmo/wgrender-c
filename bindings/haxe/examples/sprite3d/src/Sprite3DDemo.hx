// wgrender's sprite3d example, as a Haxe guest: a textured billboard in a scene.
//
// A port of examples/sprite3d.c, and the shortest statement of wgrender's resource
// rule: `new Texture(path)` makes the resource from an asset path, `new Sprite3D(texture)`
// makes the object, all in init. The texture loads on create: it comes back `Pending`
// and the sprite draws nothing until it is `Ready`, a frame or so later (longer on the
// web, where it may download). Nothing waits for it: the status only says what to
// print. Nothing here is a Haxe idiom -- it is the C flow with the casts gone.
//
//   ESC  quit
//
// The class is `Sprite3DDemo` because a module named `Sprite3D` would shadow
// `wgr.Sprite3D` inside itself.
import wgr.*;

@:expose("WgrGuest")
class Sprite3DDemo {
	static inline final SCREEN_WIDTH = 900;
	static inline final SCREEN_HEIGHT = 700;
	static inline final LOGO_PATH = "sprites/logo/wg-logo-bw-alpha.png";

	static inline final ORBIT_SPEED = 0.3;
	static inline final ORBIT_RADIUS = 13.0;
	static inline final BOB_SPEED = 1.5;
	static inline final BOB_HEIGHT = 0.8;
	static inline final BOB_CENTRE = 3.5;

	static var background:Color;
	static var scene:Scene;
	static var camera:Camera3D;
	static var logo:Texture; // kept to read its status
	static var sprite:Sprite3D;
	static var target:Vec3;
	static var up:Vec3;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {}); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "sprite3d (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(20, 22, 30, 255);
		target = new Vec3(0, 2.5, 0);
		up = new Vec3(0, 1, 0);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(12, 7, 12), target, up);
		scene = new Scene();
		scene.setActiveCamera(camera);

		// a pedestal, for somewhere to sit above and something to judge depth by
		final pedestal = new Shape3D();
		pedestal.setCube(new Vec3(3.0, 0.5, 3.0));
		pedestal.setPosition(0, 0.25, 0);
		pedestal.setColor(Color.DARKGRAY);
		scene.add(pedestal);

		logo = new Texture(LOGO_PATH); // Pending: loads over the next frames
		sprite = new Sprite3D(logo); // fine at once: drawn when the texture is Ready
		sprite.setSize(6.0);
		sprite.setFacing(Camera);
		scene.add(sprite);
		Debug.enableFps(12, 10, 16);
	}

	static function onFrame(dt:Float):Void {
		final t = Wgr.getTime();

		camera.setView(new Vec3(Math.cos(t * ORBIT_SPEED) * ORBIT_RADIUS, 7.0,
			Math.sin(t * ORBIT_SPEED) * ORBIT_RADIUS), target, up);

		sprite.setPosition(0, BOB_CENTRE + Math.sin(t * BOB_SPEED) * BOB_HEIGHT, 0);

		Render.beginFrame();
		Render.clearBackground(background);

		Render.beginMode3D();
		Shape3D.drawGrid(20, 1.0, Color.DARKGRAY);
		Render.endMode3D();

		scene.draw();

		Text.draw("wgrender + sokol — sprite3d (Haxe guest)", 12, 36, 24, Color.RAYWHITE);
		switch (logo.getStatus()) {
			case Pending: Text.draw("loading logo...", 12, 70, 16, Color.LIGHTGRAY);
			case Failed: Text.draw("logo failed to load (see the log)", 12, 70, 16, Color.RED);
			case _: Text.draw("logo: new Texture -> new Sprite3D", 12, 70, 16, Color.LIGHTGRAY);
		}

		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.getKeyboardState().isPressed(Escape))
			Wgr.requestQuit();
	}
}
