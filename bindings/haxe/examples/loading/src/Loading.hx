// wgrender's loading example, as a Haxe guest: loading during play without stalling.
//
// A port of examples/loading.c. Two environments at roughly 330 ms of CPU work each,
// two models and two textures are created while a cube spins and a graph shows every
// frame's real duration. Resources load on create: each comes back `Pending` at once
// and is `Ready` or `Failed` a few frames later, so this program creates them all, uses
// them at once, and only reads their statuses, for the progress bar and a row per file.
// Nothing is called back, and nothing waits.
//
//   A    load spread out: files are decoded on worker threads, and the GPU uploads
//        are given a few milliseconds a frame (`Asset.setUploadBudget`, 4 ms)
//   S    load at once: no upload budget, so everything decoded is uploaded in the
//        same frame, which the graph shows as a spike
//   F    create a texture whose file isn't there: `Failed` a frame or so later, drawn
//        as the placeholder (the log says why)
//   U    unload
//   ESC  quit
//
// Starts with a spread-out load.
import wgr.*;

@:expose("WgrGuest")
class Loading {
	static inline final SCREEN_WIDTH = 1100;
	static inline final SCREEN_HEIGHT = 720;
	static inline final ENVIRONMENTS = 2;
	static inline final MESHES = 2;
	static inline final TEXTURES = 2;
	static inline final FILES = ENVIRONMENTS + MESHES + TEXTURES + 1;
	static inline final MISSING = FILES - 1; // created only on F
	static inline final GRAPH = 300;

	static final PATHS = [
		"environments/venice_sunset_1k.hdr", "environments/studio_small_09_1k.hdr",
		"models/woman_casual/woman_casual.glb", "models/sphere/sphere.glb",
		"textures/tiles_normal.png", "sprites/logo/wg-logo-white-alpha.png",
		"textures/not_there.png" // missing on purpose
	];
	static final STATUS = ["", "pending", "ready", "FAILED"];

	static var background:Color;
	static var bar:Color;
	static var graphOk:Color;
	static var graphSlow:Color;
	static var line:Color;
	static var cubeColor:Color;

	static var scene:Scene;
	static var camera:Camera3D;
	static var character:Model;
	static var sphere:Model;
	static var material:Material;

	static var resources:Array<Handle> = [];

	static var loading = false;
	static var atOnce = false;
	static var loadStarted = 0.0;
	static var loadSeconds = 0.0;

	static var frameMs:Array<Float> = [];
	static var frameNext = 0;
	static var lastTime = 0.0;
	static var elapsed = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), (_, _, _) -> {}); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "loading (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		background = Color.rgba(20, 22, 28, 255);
		bar = Color.rgba(0, 0, 0, 170);
		graphOk = Color.rgba(90, 200, 120, 255);
		graphSlow = Color.rgba(235, 80, 70, 255);
		line = Color.rgba(255, 255, 255, 90);
		cubeColor = Color.rgba(230, 180, 60, 255);
		for (_ in 0...GRAPH)
			frameMs.push(0.0);
		for (_ in 0...FILES)
			resources.push(Handle.NONE);

		camera = new Camera3D(Perspective);
		camera.setView(new Vec3(0, 1.0, 5.5), new Vec3(0, 0.6, 0));
		scene = new Scene();
		scene.setActiveCamera(camera);

		character = new Model(Handle.NONE);
		character.setTransform(new Vec3(-1.2, 0, 0), new Vec3(0, 0.4, 0), new Vec3(0.5, 0.5, 0.5));
		character.setAnimation(3);
		scene.add(character);

		sphere = new Model(Handle.NONE);
		sphere.setTransform(new Vec3(1.2, 0.8, 0), Vec3.ZERO, new Vec3(0.8, 0.8, 0.8));
		material = new Material(Pbr);
		material.setBaseColor(0.9, 0.9, 0.9, 1.0);
		material.setRoughness(0.25);
		sphere.setMaterial(0, material);
		scene.add(sphere);

		lastTime = Wgr.getTime();
		startLoad(false);
	}

	static function releaseAll():Void {
		scene.setEnvironment(Handle.NONE, 1.0, 0.0);
		scene.setBackground(Handle.NONE, 0.0);
		character.setMesh(Handle.NONE);
		sphere.setMesh(Handle.NONE);
		material.setNormalTexture(Handle.NONE);
		material.setBaseColorTexture(Handle.NONE);
		for (i in 0...FILES) {
			Resource.release(resources[i]); // one call for every kind; false for none
			resources[i] = Handle.NONE;
		}
		loading = false;
	}

	/** Create every resource and use it at once: each shows up when it's `Ready`. **/
	static function startLoad(allAtOnce:Bool):Void {
		releaseAll();
		atOnce = allAtOnce;
		Asset.setUploadBudget(allAtOnce ? 1000.0 : 4.0); // 4 ms is the default
		loadStarted = Wgr.getTime();
		loading = true;
		for (i in 0...GRAPH)
			frameMs[i] = 0.0; // "worst" covers this load

		for (i in 0...ENVIRONMENTS)
			resources[i] = new Environment(PATHS[i]);
		for (i in ENVIRONMENTS...ENVIRONMENTS + MESHES)
			resources[i] = new Mesh(PATHS[i]);
		for (i in ENVIRONMENTS + MESHES...MISSING)
			resources[i] = new Texture(PATHS[i]);

		scene.setEnvironment(resources[0], 1.0, 0.0);
		scene.setBackground(resources[0], 0.3);
		character.setMesh(resources[2]);
		sphere.setMesh(resources[3]);
		material.setNormalTexture(resources[4]);
	}

	/**
		How many of the files are done (`Ready` or `Failed`, or not asked for); the load
		is over when all are.
	**/
	static function filesDone():Int {
		var done = 0;
		for (resource in resources)
			if (Resource.getStatus(resource) != Pending)
				done++;
		return done;
	}

	static function drawGraph(x:Float, y:Float, width:Float, height:Float):Void {
		final maxMs = 100.0;
		final barWidth = width / GRAPH;
		var worst = 0.0;
		Shape2D.drawRectangle(x, y, width, height, bar);
		for (i in 0...GRAPH) {
			final ms = frameMs[(frameNext + i) % GRAPH];
			final h = height * (ms < maxMs ? ms : maxMs) / maxMs;
			if (h > 0)
				Shape2D.drawRectangle(x + i * barWidth, y + height - h, barWidth > 1.0 ? barWidth : 1.0, h,
					ms > 34.0 ? graphSlow : graphOk);
			if (ms > worst)
				worst = ms;
		}
		final lineY = y + height - height * 16.7 / maxMs; // a 60 Hz frame
		Shape2D.drawLine(new Vec2(x, lineY), new Vec2(x + width, lineY), line);
		Text.draw('frame times, 0-100 ms (line: 16.7 ms)   worst: ${Math.round(worst)} ms', Std.int(x) + 6,
			Std.int(y) + 6, 10, Color.LIGHTGRAY);
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		final screen = Window.getScreenSize();
		final now = Wgr.getTime();

		frameMs[frameNext] = (now - lastTime) * 1000.0; // real time, uncapped
		frameNext = (frameNext + 1) % GRAPH;
		lastTime = now;

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();
		if (keys.isPressed(A))
			startLoad(false);
		if (keys.isPressed(S))
			startLoad(true);
		if (keys.isPressed(U))
			releaseAll();
		if (keys.isPressed(F) && resources[MISSING].isNone) {
			final missing = new Texture(PATHS[MISSING]);
			resources[MISSING] = missing;
			material.setBaseColorTexture(missing); // the placeholder, once Failed
		}
		if (loading && filesDone() == FILES) {
			loading = false;
			loadSeconds = now - loadStarted;
		}

		elapsed += dt;
		character.animate(dt);

		Render.beginFrame();
		Render.clearBackground(background);
		scene.draw();
		Render.beginMode3D();
		Shape3D.drawCubeWires(new Vec3(0, 1.9 + 0.1 * Math.sin(elapsed * 3.0), 0), new Vec3(0.5, 0.5, 0.5),
			cubeColor);
		Render.endMode3D();

		Shape2D.drawRectangle(0, 0, screen.x, 64, bar);
		Text.draw("wgrender loading   A: spread out   S: all at once   F: a missing file   U: unload", 12, 12, 12,
			Color.RAYWHITE);
		// "In the background" means worker threads, and a web build only has them on a
		// cross-origin isolated page. Without them the decode lands on this thread and
		// the graph below says so, so the example had better not claim otherwise.
		Text.draw('${Wgr.getRenderer()}  ·  decoding on '
			+ (Wgr.hasThreads() ? "worker threads" : "the main thread (no threads in this build/host)"), 12, 26, 12,
			Wgr.hasThreads() ? Color.LIGHTGRAY : Color.GOLD);

		if (loading) {
			final progress = filesDone() / FILES;
			Shape2D.drawRectangle(12, 54, 240 * progress, 12, graphOk);
			Shape2D.drawRectangleLines(12, 54, 240, 12, line);
			Text.draw('loading (${atOnce ? "all at once" : "spread out"})... ${Math.round(progress * 100)}%', 264, 54,
				12, Color.LIGHTGRAY);
		} else if (!resources[0].isNone) {
			Text.draw('loaded ${atOnce ? "all at once" : "spread out"} in ${fixed(loadSeconds, 2)} s', 12, 54, 12,
				Color.LIGHTGRAY);
		}
		for (i in 0...FILES) { // a row per file asked for: where it stands
			if (resources[i].isNone)
				continue;
			final status = Resource.getStatus(resources[i]);
			final path = PATHS[i];
			final name = path.substr(path.lastIndexOf("/") + 1);
			Text.draw(pad(STATUS[status], 8) + " " + name, 12, 80 + i * 16, 12,
				status == Ready ? Color.LIME : status == Failed ? Color.RED : Color.LIGHTGRAY);
		}
		drawGraph(12, screen.y - 132, screen.x - 24, 120);
		Render.endFrame();
	}

	/** `%-8s`: `text` left-aligned in `width` characters. **/
	static function pad(text:String, width:Int):String {
		while (text.length < width)
			text += " ";
		return text;
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
