package wgr;

#if cpp
import cpp.ConstCharStar;
#end

// wgr.h — the application: open a window, run the loop

class Wgr {
	// The lifecycle handlers live in wgr.GuestAbi on both targets now: it owns
	// wgrender's slots through the guest glue, and these setters point at them.

	/**
		An exception that escapes into wgrender's C frames takes the loop with it, and
		the report is worse the further it gets: on the web the page freezes on an
		`Uncaught [object WebAssembly.Exception]` with the message gone; natively it
		unwinds out of `main` and exits 255. So every callback wgrender makes into
		Haxe catches at the edge and logs instead, and a bad frame stays a bad frame.

		`e.message`, not `e.details()`: hxcpp has no stack to add unless the build
		defines `HXCPP_STACK_TRACE`, so `details()` says the same thing for 57 KB more
		wasm. Define it if you want stacks and can spend that.
	**/
	@:allow(wgr)
	static function report(where:String, e:haxe.Exception):Void {
		Log.error('uncaught exception in $where: ${e.message}');
	}

	/** What `initValues` returns when the library is not the one this was built for. **/
	public static inline final ERR_VERSION_MISMATCH = -100;

	/**
		Open the window and set up the loop. Call before anything else.

		Refuses on a version mismatch, as `wgr.GuestAbi.start` does for a guest —
		this is the other entry point, so it needs the same guard. wgrender's own codes
		are 0 and -1..-5, so this returns a value that cannot collide with them.
	**/
	public static function initValues(width:Int, height:Int, title:String, ?flags:WindowFlag):Int {
		if (!Version.check())
			return ERR_VERSION_MISMATCH;
		final result = Raw.wgr_init_values(width, height, title, flags == null ? 0 : (flags : Int));
		// wgrender memsets its runtime in here, so a handler set before this call is
		// gone now. Put back whatever was registered; a program that set none gets
		// nothing installed.
		GuestAbi.reinstall();
		return result;
	}

	/**
		The lifecycle, on both targets.

		These go through the guest glue (`bindings/host/wgr_guest.h`), which owns wgrender's
		lifecycle slots and dispatches to whatever is set here. That is what the js
		host already does, so routing hxcpp the same way makes one source build either
		shape: all-in-one, where the Haxe program is linked with wgrender, or as a
		guest of a wasm host.

		Setting the raw `wgr_set_frame` from a guest would replace the glue's own
		handler and stop the ABI being called at all, which is why `Raw` does not offer
		it on js. Going through the glue is the version that cannot do that.
	**/

	/** Runs once, after the window exists and before the first frame. **/
	public static function setInit(cb:() -> Void):Void
		GuestAbi.setInit(cb);

	/**
		Runs every frame: `dt` seconds since the last one, and how far into the next
		tick it is. The glue reports a frame id instead, so `tickFraction` is read
		beside it — the same number wgrender would have passed.
	**/
	public static function setFrame(cb:(dt:Float, tickFraction:Float) -> Void):Void
		GuestAbi.setFrame((dt, _) -> cb(dt, GuestAbi.tickFraction()));

	/**
		Runs at a fixed rate, 0 to N times before each frame, always with `dt` of 1/`hz`
		— physics and gameplay rules. Never draw here; draw in the frame callback, where
		`tickFraction` says how far into the next tick it is, for interpolating.

		After a stall at most 5 ticks run per frame and the rest of the backlog is
		dropped, so simulation time falls behind instead of snowballing. Input edges
		belong to whichever callback reads them, and every press is seen by exactly one
		tick. An `hz` of 0 or less turns the tick off.
	**/
	public static function setTick(cb:(dt:Float) -> Void, hz:Int):Void
		GuestAbi.registerTick(cb, hz);

	/** Runs as the window closes, before the GPU is torn down. **/
	public static function setShutdown(cb:() -> Void):Void
		GuestAbi.setShutdown(cb);

	#if cpp
	/**
		Drive the loop; hxcpp only, because on js the host owns it and there is nothing
		to return from. A js program calls `GuestAbi.start` instead, which is
		`initValues` and this in one.
	**/
	public static inline function run():Int {
		return Raw.wgr_run();
	}
	#end

	/** True once `initValues` has succeeded. **/
	public static inline function isInitialized():Bool
		return Raw.wgr_is_initialized();

	/**
		Which renderer is running: "GL core", "GLES3/WebGL2", "WebGPU", "D3D11",
		"Metal (macOS)" or "headless", and "none" before `run` starts. Display text —
		it names the backend wgrender chose, which the API otherwise hides.
	**/
	public static inline function getRenderer():String
		return Raw.wgr_get_renderer();

	/**
		Whether assets decode and upload off the main thread, so a program can say why
		something is missing instead of quietly behaving differently. A web build has
		threads only if it was built with them *and* the page is cross-origin isolated,
		which needs COOP/COEP headers from the host; a static host that can't send them
		serves the single-threaded build, where loading blocks the frame it happens on.
	**/
	public static inline function hasThreads():Bool
		return Raw.wgr_has_threads();

	/** Close the window / end the loop. **/
	public static inline function requestQuit():Void {
		Raw.wgr_request_quit();
	}

	public static inline function getPlatform():String {
		return Raw.wgr_get_platform().toString();
	}

	/**
		Who the program is: the directories its own files go under on desktop, the asset
		cache now (`Asset.getCacheDir`), as <user's cache>/<company>/<app>. The company
		is "DefaultCompany" until set, so nothing unset looks like anyone's; the app is
		the executable's name. Set both for anything shipped, before the asset host: two
		programs left with the defaults and the same name share a cache. Each is made
		one safe path component; null, or a name with nothing left, is the default again.
	**/
	public static inline function setAppCompany(company:String):Void {
		Raw.wgr_set_app_company(company);
	}

	public static inline function getAppCompany():String {
		return Raw.wgr_get_app_company().toString();
	}

	public static inline function setAppName(name:String):Void {
		Raw.wgr_set_app_name(name);
	}

	public static inline function getAppName():String {
		return Raw.wgr_get_app_name().toString();
	}

	public static inline function setTargetFps(fps:Int):Void {
		Raw.wgr_set_target_fps(fps);
	}

	/** Seconds since the program started. Can be called at any time. **/
	public static inline function getTime():Float {
		return Raw.wgr_get_time();
	}
}
