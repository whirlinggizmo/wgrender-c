// wgrender's stress scene (wgrender-c tools/bench/stress.c), as a Haxe guest: N entities
// updated every frame, a steady churn of them dying and being replaced, and a screenful
// of formatted text. It follows the C's spec line for line, so every language does the
// same work, and it is written the way Haxe naturally would be: an entity is a class
// instance, and a replaced one is a new instance for the GC to collect.
import wgr.*;

private class Entity {
	public var x:Float;
	public var y:Float;
	public var z:Float;
	public var vx:Float;
	public var vy:Float;
	public var vz:Float;
	public var angle = 0.0;
	public var spin:Float;
	public var life:Float;
	public var sprite:Sprite3D;

	public function new(sprite:Sprite3D) {
		x = (Stress.rnd() * 2 - 1) * Stress.BOX / 2;
		y = 1 + Stress.rnd() * 4;
		z = (Stress.rnd() * 2 - 1) * Stress.BOX / 2;
		vx = (Stress.rnd() * 2 - 1) * 4;
		vy = 4 + Stress.rnd() * 6;
		vz = (Stress.rnd() * 2 - 1) * 4;
		spin = (Stress.rnd() * 2 - 1) * 3;
		life = 2 + Stress.rnd() * 4;
		this.sprite = sprite;
	}
}

@:expose("WgrGuest")
#if emscripten
@:cppFileCode("#include <emscripten.h>")
#end
class Stress {
	static inline final SCREEN_WIDTH = 1024;
	static inline final SCREEN_HEIGHT = 1280;
	static inline final SPRITE_PATH = "sprites/logo/wg-logo-bw-alpha.png";
	static inline final DEFAULT_N = 2000;
	static inline final STEP = 1.0 / 60.0;
	public static inline final BOX = 10.0;
	static inline final TEXT_LINES = 48;
	static final UP = new Vec3(0, 1, 0);

	static var n = DEFAULT_N;
	static var rng = 0;
	static var entities:Array<Entity> = null;
	static var texture:Texture;
	static var scene:Scene;
	static var background:Color;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		n = entityCount();
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (_, _) -> onFrame()); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "stress (wgrender host, Haxe guest)", Resizable);
	}

	/** ?n= in the page's URL on the web, the first argument (or STRESS_N) on desktop. **/
	static function entityCount():Int {
		#if js
		final given = Std.parseInt(new js.html.URLSearchParams(js.Browser.location.search).get("n"));
		#elseif emscripten
		// all-in-one through hxcpp (tools/build_hxcpp_example.py): C++ in the page, no argv
		final given:Null<Int> = untyped __cpp__('emscripten_run_script_int("+(new URLSearchParams(location.search).get(\'n\')) || 0")');
		#else
		final args = Sys.args();
		final given = Std.parseInt(args.length > 0 ? args[0] : Sys.getEnv("STRESS_N"));
		#end
		return given != null && given > 0 ? given : DEFAULT_N;
	}

	/** xorshift32, as the spec gives it: logical shifts on 32 bits. **/
	public static function rnd():Float {
		var x = rng;
		x ^= x << 13;
		x ^= x >>> 17;
		x ^= x << 5;
		rng = x;
		return (x >>> 8) / 16777216.0;
	}

	static function spawn():Entity {
		final e = new Entity(new Sprite3D(texture));
		e.sprite.setFacing(Free);
		scene.add(e.sprite);
		return e;
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		Log.setLevel(Warn);
		Wgr.setTargetFps(60);
		rng = 0x92D68CA2; // 2463534242

		final camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 14, 30), new Vec3(0, 3, 0), UP);
		scene = new Scene();
		scene.setActiveCamera(camera);
		background = Color.rgba(245, 245, 245, 255);

		texture = new Texture(SPRITE_PATH); // the sprites are drawn once it's loaded
		entities = [for (_ in 0...n) spawn()];
	}

	static function update(i:Int):Void {
		final e = entities[i];
		e.vy -= 9.8 * STEP;
		e.x += e.vx * STEP;
		e.y += e.vy * STEP;
		e.z += e.vz * STEP;
		if (e.y < 0) {
			e.y = 0;
			e.vy = -e.vy * 0.8;
		}
		if (Math.abs(e.x) > BOX) {
			e.x = e.x > 0 ? BOX : -BOX;
			e.vx = -e.vx;
		}
		if (Math.abs(e.z) > BOX) {
			e.z = e.z > 0 ? BOX : -BOX;
			e.vz = -e.vz;
		}
		e.angle += e.spin * STEP;
		e.life -= STEP;
		e.sprite.setTransform(new Vec3(e.x, e.y, e.z), new Vec3(0, e.angle, 0), new Vec3(0.5, 0.5, 0.5));
		if (e.life <= 0) {
			e.sprite.destroy();
			entities[i] = spawn(); // a new instance; the old one is garbage
		}
	}

	/** A number to 2 decimals, as the C's %.2f. **/
	static function fixed2(v:Float):String {
		final hundredths = Math.round(Math.abs(v) * 100);
		final frac = hundredths % 100;
		return (v < 0 && hundredths != 0 ? "-" : "") + Std.int(hundredths / 100) + "." + (frac < 10 ? "0" : "") + frac;
	}

	static function drawText():Void {
		Text.draw('stress: $n entities', 10, 10, 16, Color.BLACK);
		if (entities == null)
			return;
		for (i in 0...(TEXT_LINES < n ? TEXT_LINES : n)) {
			final e = entities[i];
			Text.draw('e$i: ${fixed2(e.x)} ${fixed2(e.y)} ${fixed2(e.z)} life ${fixed2(e.life)}', 10, 34 + 18 * i, 16, Color.BLACK);
		}
	}

	static function onFrame():Void {
		if (entities != null)
			for (i in 0...n)
				update(i);
		Render.beginFrame();
		Render.clearBackground(background);
		scene.draw();
		drawText();
		Render.endFrame();
	}
}
