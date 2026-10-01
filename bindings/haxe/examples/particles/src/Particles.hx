// wgrender's particles example as a guest module — a port of examples/particles.c.
//
// Three 3D emitters in a scene: a fountain of blended drops under gravity, additive
// sparks from a source circling it (thrown along by it, slowed by drag, stretched
// along their motion), and a campfire — flipbook flames under smoke driven by size and
// colour curves. Click for a 2D confetti burst; space pauses the steady ones; O stops
// and restarts the camera's orbit.
//
// The second example on the wgrender-hx binding, and the reason for most of what it
// knows about emitters.
import wgr.*;

@:expose("WgrGuest")
class Particles {
	static inline final PARTICLE_PATH = "textures/particle.png";
	static inline final FLAME_PATH = "textures/flame.png"; // a 4x4 flipbook

	static inline final ASSET_PARTICLE = 1;
	static inline final ASSET_FLAME = 2;

	static inline final SCREEN_WIDTH = 1000;
	static inline final SCREEN_HEIGHT = 700;
	static inline final ORBIT_RADIUS = 16.0;
	static inline final ORBIT_SPEED = 0.15; // radians per second

	static var scene:Scene;
	static var camera:Camera3D;
	static var fountain:Emitter3D;
	static var sparks:Emitter3D;
	static var smoke:Emitter3D;
	static var flame:Emitter3D;
	static var confetti:Emitter2D;

	static var paused = false;
	static var orbit = true;
	static var orbitAngle = 0.0;
	static var elapsed = 0.0;
	static var background:Color = Color.BLACK;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Void {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "particles (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	// --- lifecycle ---

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		Log.setLevel(Warn);
		background = Color.rgba(14, 16, 24, 255);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 6, 16), new Vec3(0, 3, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);

		load(PARTICLE_PATH, ASSET_PARTICLE);
		load(FLAME_PATH, ASSET_FLAME);
		Debug.enableFps(12, 10, 16);
	}

	static function load(path:String, id:Int):Void {
		if (!GuestAbi.loadAsset(path, id))
			Log.error('failed to queue asset: $path');
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('asset load failed: $path');
			return;
		}
		final texture = new Texture(path);
		if (texture.isNone())
			return;
		switch id {
			case ASSET_PARTICLE:
				makeFountain(texture);
				makeSparks(texture);
				makeSmoke(texture);
				makeConfetti(texture);
				final screen = Window.getScreenSize();
				burst(new Vec2(screen.x * 0.5, screen.y * 0.4)); // one to start with

			case ASSET_FLAME:
				makeFlame(texture);
		}
		texture.release(); // each emitter holds its own reference
	}

	// --- the emitters ---

	static function makeFountain(texture:Texture):Void {
		fountain = new Emitter3D(texture);
		fountain.setMax(4096);
		fountain.setRate(1200);
		fountain.setLife(1.4, 1.9);
		fountain.setPosition(0, 0.2, 0);
		fountain.setSpawnBox(new Vec3(0.15, 0, 0.15));
		fountain.setVelocity(new Vec3(0, 9, 0), 0.22, 0.15);
		fountain.setGravity(new Vec3(0, -9.8, 0));
		fountain.setSize(0.22, 0.12, 0.4);
		fountain.setColor(Color.rgba(150, 210, 255, 230), Color.rgba(60, 120, 255, 0));
		fountain.setAlphaMode(Blend);
		fountain.prewarm(2.0); // already running when the page opens
		scene.add(fountain);
	}

	static function makeSparks(texture:Texture):Void {
		sparks = new Emitter3D(texture);
		sparks.setMax(2048);
		sparks.setRate(600);
		sparks.setLife(0.5, 1.2);
		sparks.setVelocity(new Vec3(0, 4, 0), 1.2, 0.6);
		sparks.setGravity(new Vec3(0, -6, 0));
		sparks.setDrag(1.5); // they slow down
		sparks.setInheritVelocity(0.4); // thrown along by the moving source
		sparks.setStretch(0.04); // streaks along their motion
		sparks.setSize(0.08, 0.02, 0.5);
		sparks.setColor(Color.rgba(255, 220, 120, 255), Color.rgba(255, 60, 10, 0));
		// Add is the default
		scene.add(sparks);
	}

	static function makeSmoke(texture:Texture):Void {
		smoke = new Emitter3D(texture);
		smoke.setMax(512);
		smoke.setRate(30);
		smoke.setLife(3.0, 4.5);
		smoke.setPosition(-5, 1.5, -2); // above the fire
		smoke.setSpawnSphere(0.3);
		smoke.setVelocity(new Vec3(0, 1.4, 0), 0.35, 0.3);
		smoke.setGravity(new Vec3(0.35, 0, 0)); // a breeze
		smoke.setSpin(-0.8, 0.8);
		// curves: a quick puff that keeps spreading; dark, then light, then gone
		smoke.clearSizeKeys();
		smoke.addSizeKey(0.0, 0.3);
		smoke.addSizeKey(0.15, 1.4);
		smoke.addSizeKey(1.0, 3.6);
		smoke.clearColorKeys();
		smoke.addColorKey(0.0, Color.rgba(40, 36, 34, 0));
		smoke.addColorKey(0.1, Color.rgba(50, 46, 44, 190));
		smoke.addColorKey(0.5, Color.rgba(130, 130, 140, 120));
		smoke.addColorKey(1.0, Color.rgba(170, 170, 180, 0));
		smoke.setAlphaMode(Blend);
		smoke.prewarm(5.0);
		scene.add(smoke);
	}

	static function makeFlame(texture:Texture):Void {
		flame = new Emitter3D(texture);
		flame.setFrames(4, 4);
		flame.setRate(40);
		flame.setLife(0.7, 1.1);
		flame.setPosition(-5, 0.3, -2);
		flame.setSpawnSphere(0.3);
		flame.setVelocity(new Vec3(0, 1.8, 0), 0.2, 0.3);
		flame.setDrag(0.8);
		flame.setSpin(-1.5, 1.5);
		flame.clearSizeKeys();
		flame.addSizeKey(0.0, 0.6);
		flame.addSizeKey(0.3, 1.1);
		flame.addSizeKey(1.0, 0.4);
		flame.clearColorKeys();
		flame.addColorKey(0.0, Color.rgba(255, 235, 190, 0));
		flame.addColorKey(0.08, Color.rgba(255, 235, 190, 110));
		flame.addColorKey(0.25, Color.rgba(255, 150, 40, 100));
		flame.addColorKey(0.65, Color.rgba(200, 50, 15, 60));
		flame.addColorKey(1.0, Color.rgba(80, 15, 5, 0));
		flame.prewarm(1.0);
		scene.add(flame); // added: Add, the default
	}

	static function makeConfetti(texture:Texture):Void {
		confetti = new Emitter2D(texture);
		confetti.setSource(24, 24, 16, 16); // the dot's solid middle: squares
		confetti.setMax(4096);
		confetti.setLife(1.2, 2.2);
		confetti.setVelocity(new Vec2(0, -420), 1.3, 0.7);
		confetti.setGravity(new Vec2(0, 700));
		confetti.setDrag(0.8); // flutters down instead of dropping
		confetti.setSize(12, 6, 0.5);
		confetti.setSpin(-8, 8);
		confetti.setColor(Color.WHITE, Color.rgba(255, 255, 255, 0));
		// each piece picks one of these at birth
		for (color in [Color.RED, Color.GOLD, Color.LIME, Color.SKYBLUE, Color.VIOLET])
			confetti.addPaletteColor(color);
		confetti.setAlphaMode(Blend);
		scene.add(confetti, 1);
	}

	static function burst(at:Vec2):Void {
		confetti.jump(at);
		confetti.burst(300);
	}

	static function setPaused(value:Bool):Void {
		paused = value;
		for (emitter in [fountain, sparks, smoke, flame])
			if (!emitter.isNone())
				emitter.setEmitting(!value);
	}

	// --- frame ---

	static function onFrame(dt:Float):Void {
		elapsed += dt;

		// the sparks' source circles the fountain; the sparks stay where they were born
		if (!sparks.isNone())
			sparks.setPosition(3.5 * Math.cos(elapsed * 1.3), 1.5 + 0.8 * Math.sin(elapsed * 2.1),
				3.5 * Math.sin(elapsed * 1.3));

		final mouse = Input.getMouseState();
		if (mouse.left == ButtonState.Pressed && !confetti.isNone())
			burst(new Vec2(mouse.x, mouse.y));
		if (Input.isKeyPressed(Space) && !fountain.isNone())
			setPaused(!paused);
		if (Input.isKeyPressed(O))
			orbit = !orbit;
		if (orbit)
			orbitAngle += dt * ORBIT_SPEED;
		camera.setView(new Vec3(ORBIT_RADIUS * Math.sin(orbitAngle), 6, ORBIT_RADIUS * Math.cos(orbitAngle)),
			new Vec3(0, 3, 0));

		Render.beginFrame();
		Render.clearBackground(background);

		Render.beginMode3D();
		Shape3D.drawGrid(24, 1.0, Color.DARKGRAY);
		Render.endMode3D();

		scene.draw();

		Text.draw("wgrender + sokol — particles, from Haxe", 12, 36, 24, Color.RAYWHITE);
		Text.draw('click / tap: confetti   space: ${paused ? "resume" : "pause"}   '
			+ 'O: ${orbit ? "stop" : "turn"} the camera', 12, 70, 16, Color.LIGHTGRAY);
		Text.draw('fountain ${fountain.getCount()}   sparks ${sparks.getCount()}   flame ${flame.getCount()}   '
			+ 'smoke ${smoke.getCount()}   confetti ${confetti.getCount()}', 12, 94, 16, Color.LIGHTGRAY);

		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
	}
}
