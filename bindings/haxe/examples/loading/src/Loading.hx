// wgrender's loading example, as a Haxe guest: loading during play without stalling.
//
// A port of examples/loading.c. Six files — two environments at roughly 330 ms of CPU
// work each, two models and two textures — load while a cube spins and a graph shows
// every frame's real duration. The graph is the point: a background load should leave
// it flat.
//
//   A    in the background: files are decoded on worker threads and uploaded a few
//        milliseconds per frame, so creating them at the end is cheap
//   S    synchronously, for comparison: the files are only *fetched* (`FileOnly`) and
//        every one is created in a single frame, which the graph shows as one spike
//   U    unload
//   ESC  quit
//
// The C builds an asset group and hangs two callbacks off it: one per file to keep the
// local path, one on the group to know when they are all in. Neither crosses to js —
// `wgr_asset_add_task` takes a C callback and is one of the calls the guest ABI
// replaces. It replaces them both: the asset op reports every file by id with its
// local path, so counting the ids *is* the group, and progress is the count over six.
// The group calls do reach js (`Asset.createGroup`, `groupAdd`, `getProgress`); they
// are simply not needed once each file announces itself.
import wgr.*;

@:expose("WgrGuest")
class Loading {
	static inline final SCREEN_WIDTH = 1100;
	static inline final SCREEN_HEIGHT = 720;
	static inline final ENVIRONMENTS = 2;
	static inline final MESHES = 2;
	static inline final GRAPH = 300;

	static final PATHS = [
		"environments/venice_sunset_1k.hdr", "environments/studio_small_09_1k.hdr",
		"models/woman_casual/woman_casual.glb", "models/sphere/sphere.glb",
		"textures/tiles_normal.png", "sprites/logo/wg-logo-white-alpha.png"
	];

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

	static var paths:Array<String> = [];
	static var environments:Array<Environment> = [];
	static var meshes:Array<Mesh> = [];
	static var textures:Array<Texture> = [];

	static var loading = false;
	static var sync = false;
	static var arrived = 0;
	static var loaded = false;
	static var loadStarted = 0.0;
	static var loadSeconds = 0.0;
	static var createMs = 0.0;

	static var frameMs:Array<Float> = [];
	static var frameNext = 0;
	static var lastTime = 0.0;
	static var elapsed = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
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
		for (e in environments)
			e.release();
		for (m in meshes)
			m.release();
		for (t in textures)
			t.release();
		environments = [];
		meshes = [];
		textures = [];
		loaded = false;
	}

	/**
		Create every resource from its local path, and use them.

		In a background load each create finds what the pipeline already prepared, so
		this is cheap; after a `FileOnly` load it is where all the decoding happens, in
		one frame, which is the spike the graph shows.
	**/
	static function createAll():Void {
		final start = Wgr.getTime();
		for (i in 0...ENVIRONMENTS)
			environments.push(new Environment(PATHS[i])); // an asset path: loads on create
		for (i in ENVIRONMENTS...ENVIRONMENTS + MESHES)
			meshes.push(new Mesh(paths[i]));
		for (i in ENVIRONMENTS + MESHES...PATHS.length)
			textures.push(new Texture(PATHS[i])); // an asset path: loads on create
		createMs = (Wgr.getTime() - start) * 1000.0;

		scene.setEnvironment(environments[0], 1.0, 0.0);
		scene.setBackground(environments[0], 0.3);
		character.setMesh(meshes[0]);
		sphere.setMesh(meshes[1]);
		material.setNormalTexture(textures[0]);
		loaded = true;
	}

	static function startLoad(synchronous:Bool):Void {
		if (loading)
			return; // one load at a time
		releaseAll();
		sync = synchronous;
		loading = true;
		arrived = 0;
		paths = [for (_ in 0...PATHS.length) ""];
		loadStarted = Wgr.getTime();
		for (i in 0...GRAPH)
			frameMs[i] = 0.0; // "worst" should describe this load, not the last one
		createMs = 0.0;
		// FileOnly fetches without creating anything, so the work lands in createAll.
		for (i in 0...PATHS.length)
			GuestAbi.loadAsset(PATHS[i], i + 1, null, synchronous ? FileOnly : null);
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!loading || id < 1 || id > PATHS.length)
			return;
		if (!ok) {
			Log.error('loading: $path failed');
			loading = false;
			return;
		}
		paths[id - 1] = path;
		if (++arrived < PATHS.length)
			return;
		loading = false;
		createAll();
		loadSeconds = Wgr.getTime() - loadStarted;
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
		if (keys.isPressed(U) && !loading)
			releaseAll();

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
		Text.draw("wgrender loading   A: in the background   S: synchronously   U: unload", 12, 12, 12,
			Color.RAYWHITE);
		// "In the background" means worker threads, and a web build only has them on a
		// cross-origin isolated page. Without them the decode lands on this thread and
		// the graph below says so, so the example had better not claim otherwise.
		Text.draw('${Wgr.getRenderer()}  ·  decoding on '
			+ (Wgr.hasThreads() ? "worker threads" : "the main thread (no threads in this build/host)"), 12, 26, 12,
			Wgr.hasThreads() ? Color.LIGHTGRAY : Color.GOLD);

		if (loading) {
			final progress = arrived / PATHS.length;
			Shape2D.drawRectangle(12, 54, 240 * progress, 12, graphOk);
			Shape2D.drawRectangleLines(12, 54, 240, 12, line);
			Text.draw('loading (${sync ? "synchronously" : (Wgr.hasThreads() ? "in the background"
				: "in the background, but on this thread")})... ${Math.round(progress * 100)}%', 264, 54, 12,
				Color.LIGHTGRAY);
		} else if (loaded) {
			Text.draw('loaded ${PATHS.length} files '
				+ '${sync ? "synchronously" : (Wgr.hasThreads() ? "in the background" : "without threads")} '
				+ 'in ${fixed(loadSeconds, 2)} s; creating them took ${Math.round(createMs)} ms', 12, 54, 12,
				Color.LIGHTGRAY);
		}
		drawGraph(12, screen.y - 132, screen.x - 24, 120);
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
