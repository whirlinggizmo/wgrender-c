// wgrender simple, as a guest module: the same scene as ../simple/src/Simple.hx, but
// compiled to JS and run on top of the wgrender wasm host rather than inside it.
//
// The differences from the hxcpp port are all at the edges — how the lifecycle is
// entered (the guest ABI, not wgr_set_init/wgr_set_frame). The scene code between those
// edges is the same, against the same wgr.Wgr layer.
import wgr.*;

@:expose("WgrGuest")
class Guest {
	static inline final DEBUG_FONT_PATH = "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf";
	static inline final KOMIKA_FONT_PATH = "fonts/Komika/KOMIKAH_.ttf";
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";
	static inline final SPRITE_PATH = "sprites/logo/wg-logo-bw-alpha.png";
	static inline final MUSIC_PATH = "music/a_hero_is_born.mp3";

	static inline final SCREEN_WIDTH = 1024;
	static inline final SCREEN_HEIGHT = 1280;
	static inline final DEBUG_FONT_SIZE = 18;
	static inline final KOMIKA_FONT_SIZE = 24;

	static inline final SPRITE_Y_OFFSET = 3.0;
	static inline final BOB_SPEED = 1.0;
	static inline final BOB_HEIGHT = 1.5;

	static var elapsed = 0.0;
	static var countdownTimer = 0.0;
	static var debugFont:Font;
	static var greyAlpha:Color = Color.BLACK;
	static var komikaFont:Font;
	static var sprite:Sprite3D;
	static var model:Model;
	static var bgm:Sound;
	static var camera:Camera3D;
	static var scene:Scene;
	static var backgroundColor:Color = Color.RAYWHITE;
	static var message = "";
	static var platformText = "";

	/**
		On desktop this is the entry point and `autostart` runs the guest now; on the
		web the page owns startup, so `autostart` does nothing and web/boot.js calls
		`start` once the host module has loaded.
	**/
	static function main():Void {
		GuestAbi.autostart(start);
	}

	// --- the guest ABI (bindings/host/wgr_guest.h) ---

	/**
		Register the ops, then start the host. `wgr.GuestAbi` is per-target — the JS
		one installs them through `addFunction` on the Emscripten module, the hxcpp one
		through plain function pointers — so nothing below this line is target-specific.
	**/
	public static function start(host:Dynamic):Void {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt)); // ensures nothing
		GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "simple (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	// --- lifecycle ---

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		Log.setLevel(Warn);
		Wgr.setTargetFps(60);

		countdownTimer = 30.0;
		message = "Hello from wgrender simple (Haxe guest)!";
		platformText = 'Platform: ${Wgr.getPlatform()}';

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(12, 12, 12), new Vec3(0, 1, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);

		final sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.6, -1.0, -0.5));
		sun.setIntensity(3.0);
		scene.add(sun);
		scene.setAmbient(Color.WHITE, 0.25);
		backgroundColor = Color.rgba(245, 245, 245, 255);
		greyAlpha = Color.rgba(0, 0, 0, 128);

		makeBgm();
		makeModel();
		makeSprite();
		// fonts are sized per draw call, so one handle serves any size; text is in the
		// built-in font until they've loaded
		debugFont = new Font(DEBUG_FONT_PATH);
		komikaFont = new Font(KOMIKA_FONT_PATH);
	}

	// A texture loads on create: the sprite exists at once and is drawn once it's loaded.
	static function makeSprite():Void {
		final texture = new Texture(SPRITE_PATH);
		sprite = new Sprite3D(texture);
		texture.release(); // the sprite holds its own reference
		sprite.setFacing(Free);
		sprite.setPosition(0, SPRITE_Y_OFFSET, 0);
		sprite.setTint(Color.RAYWHITE);
		scene.add(sprite);
	}

	// Audio loads on create: the music plays once it has loaded.
	static function makeBgm():Void {
		final audio = new Audio(MUSIC_PATH);
		bgm = new Sound(audio);
		audio.release(); // the sound holds its own reference
		bgm.setLoop(true);
		bgm.play();
	}

	// A mesh loads on create: the model is drawn once it has loaded.
	static function makeModel():Void {
		final mesh = new Mesh(CHARACTER_PATH);
		model = new Model(mesh);
		mesh.release(); // the model holds its own reference
		model.setAnimation(1);
		model.setAnimationSpeed(1.0);
		model.setAnimationLoop(true);
		model.setPosition(0, 0, 0);
		model.setTint(Color.RAYWHITE);
		scene.add(model);
	}

	static function update(dt:Float):Void {
		elapsed += dt;
		countdownTimer -= dt;

		if (!model.isNone())
			model.animate(dt);
		if (!sprite.isNone()) {
			final y = Math.sin(elapsed * BOB_SPEED) * BOB_HEIGHT + SPRITE_Y_OFFSET;
			sprite.setPosition(0, y, 0);
		}
	}

	static function updatePickMessage(mouse:MouseState):Void {
		final pick = scene.pick(mouse.x, mouse.y);
		final what = if (!pick.hit) "" else if (pick.handle == model) "Model" else if (pick.handle == sprite)
			"Sprite" else "";
		if (what == "") {
			message = "Nothing picked!";
			return;
		}
		message = '$what pick: Mouse position (mouse.x:${mouse.x}, mouse.y:${mouse.y}) '
			+ 'pick result y: ${fixed(pick.pointWorld.y, 6)}';
	}

	static function drawText(font:Font, text:String, x:Float, y:Float, size:Int, color:Color):Void {
		if (!font.isNone())
			font.draw(text, x, y, size, color);
		else
			Text.draw(text, Std.int(x), Std.int(y), size, color);
	}

	static function drawCenteredMessage():Void {
		final screen = Window.getScreenSize();
		final size = !komikaFont.isNone() ? komikaFont.measure(message,
			KOMIKA_FONT_SIZE) : new Vec2(Text.measure(message, KOMIKA_FONT_SIZE), KOMIKA_FONT_SIZE);
		drawText(komikaFont, message, (screen.x - size.x) / 2, (screen.y - size.y) / 2, KOMIKA_FONT_SIZE, Color.BLUE);
	}

	static function drawOverlay(mouse:MouseState):Void {
		drawText(debugFont, 'Remaining: ${fixed(countdownTimer, 2)}', 10, 36, DEBUG_FONT_SIZE, Color.BLACK);
		drawText(debugFont, 'Elapsed: ${fixed(elapsed, 2)}', 10, 56, DEBUG_FONT_SIZE, Color.BLACK);
		drawText(debugFont, 'Mouse: (${mouse.x}, ${mouse.y}) w:${fixed(mouse.wheel, 1)} '
			+ 'b:[${mouse.left}, ${mouse.right}, ${mouse.middle}]', 10, 76, DEBUG_FONT_SIZE, Color.BLACK);
		drawText(debugFont, platformText, 10, 96, DEBUG_FONT_SIZE, Color.BLACK);

		debugFont.drawFps(10, 10, DEBUG_FONT_SIZE, greyAlpha);
	}

	static function onFrame(dt:Float):Void {
		final mouse = Input.getMouseState();

		update(dt);
		updatePickMessage(mouse);

		Render.beginFrame();
		Render.clearBackground(backgroundColor);
		scene.draw();
		drawCenteredMessage();
		drawOverlay(mouse);
		Render.endFrame();
	}

	/** The same by-hand formatter ../simple uses, so the two read alike. **/
	static function fixed(value:Float, decimals:Int):String {
		final negative = value < 0;
		var digits = Std.string(Math.round(Math.abs(value) * Math.pow(10, decimals)));
		while (digits.length <= decimals)
			digits = "0" + digits;
		final point = digits.length - decimals;
		return (negative ? "-" : "") + digits.substr(0, point) + (decimals > 0 ? "." + digits.substr(point) : "");
	}
}
