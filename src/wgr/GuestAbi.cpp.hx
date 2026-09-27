package wgr;

// The one public module that reaches into impl: the guest ABI is this
// binding's own C, not wgrender's, so its externs live beside the generated
// surface rather than with the API.
import wgr.impl.GuestRaw;

/**
	The guest side of `host/wgr_guest.h`, for the hxcpp target: the host is the same C
	glue compiled into this binary, and the ops are plain function pointers.

	The counterpart of `GuestAbi.js.hx`. There is no scratch arena here — hxcpp passes
	strings and structs directly — but the ops still catch, because an exception that
	escapes into wgrender's C frames unwinds out of `main` and exits 255.

	Note what is *not* different: there is no wasm, no module to load and no host
	process. On desktop the Haxe program is the entry point, and `autostart` runs the
	guest from `main` where the page's boot script would on the web.
**/
class GuestAbi {
	/** Nothing to attach: the host is linked into this binary. **/
	public static function attach(_:Dynamic):Void {}

	static var onInit:() -> Void;
	static var onFrame:(dt:Float, frameId:Int) -> Void;
	static var onTick:(dt:Float) -> Void;
	static var onAsset:(id:Int, path:String, ok:Bool) -> Void;
	static var onShutdown:() -> Void;

	/**
		Declare the ops, all of them at once.

		Every slot is written, including `shutdown` when it is left out — so this
		*clears* a handler set earlier by `setShutdown` or `wgr.Wgr.setCleanup`. That
		is what "these are my ops" means, and it is the reason to reach for the
		single-op setters instead when changing one and leaving the rest alone.

		`shutdown` is optional and runs once, after the last frame, on the way out --
		wgrender's `wgr_set_cleanup` for a guest. It is the op to release anything the
		guest owns outside wgrender; anything wgrender owns is already being torn down.
	**/
	public static function register(init:() -> Void, frame:(dt:Float, frameId:Int) -> Void,
			asset:(id:Int, path:String, ok:Bool) -> Void, ?shutdown:() -> Void):Void {
		onInit = init;
		onFrame = frame;
		onAsset = asset;
		onShutdown = shutdown;
		installOps();
	}

	static var opsInstalled = false;

	/** Hand wgrender the dispatchers, once; they read the statics when they fire. **/
	static function ensureInstalled():Void {
		if (!opsInstalled)
			installOps();
	}

	static function installOps():Void {
		opsInstalled = true;
		GuestRaw.wgr_guest_register(cpp.Callable.fromStaticFunction(initOp), cpp.Callable.fromStaticFunction(frameOp),
			cpp.Callable.fromStaticFunction(assetOp), cpp.Callable.fromStaticFunction(shutdownOp));
		GuestRaw.wgr_guest_install();
	}

	/**
		Put the ops back after something wiped them.

		`wgr_init_values` memsets wgrender's runtime, so a handler set before it is
		gone afterwards. `wgr.Wgr.initValues` calls this on the way out. Only reapplies
		what was actually registered: a program that never used the ops gets nothing
		installed, and keeps whatever it set itself.
	**/
	public static function reinstall():Void {
		if (opsInstalled)
			GuestRaw.wgr_guest_install();
	}

	/**
		The ops, one at a time, after `register` or instead of it.

		The op installed with wgrender is a dispatcher that reads these statics when it
		fires, so re-pointing one is an assignment: nothing is re-registered, and on js
		no second function enters the wasm table. That is what lets `wgr.Wgr`'s
		lifecycle setters mean the same thing on both targets -- they land here.

		`ensureInstalled` is why they can be used without `register`: the slots have to
		be filled on wgrender's side before a frame happens, and `start` is the only
		thing that would otherwise do it.
	**/
	public static function setInit(init:() -> Void):Void {
		onInit = init;
		ensureInstalled();
	}

	public static function setFrame(frame:(dt:Float, frameId:Int) -> Void):Void {
		onFrame = frame;
		ensureInstalled();
	}

	public static function setShutdown(shutdown:() -> Void):Void {
		onShutdown = shutdown;
		ensureInstalled();
	}

	public static function setAsset(asset:(id:Int, path:String, ok:Bool) -> Void):Void {
		onAsset = asset;
		ensureInstalled();
	}


	/**
		Fixed-rate simulation: `tick` runs 0..N times before each frame, always with
		`dt` of 1/`hz`. Optional, and set on its own because it is the one op that
		needs configuring. `hz` of 0 or less turns it off.

		Draw in the frame op, not here, and use `tickFraction` there to interpolate
		between the last two tick states.
	**/
	public static function registerTick(tick:(dt:Float) -> Void, hz:Int):Void {
		onTick = tick;
		GuestRaw.wgr_guest_register_tick(cpp.Callable.fromStaticFunction(tickOp), hz);
	}

	/** How far this frame is into the next tick, 0..1. 0 without a tick. **/
	public static inline function tickFraction():Float
		return GuestRaw.wgr_guest_tick_fraction();

	static function tickOp(dt:Single):Int {
		if (onTick == null)
			return 0;
		try
			onTick(dt)
		catch (e:haxe.Exception) {
			Log.error('guest: uncaught exception in tick: ${e.message}');
			return 1;
		}
		return 0;
	}

	static function initOp():Int {
		if (onInit == null)
			return 0;
		try
			onInit()
		catch (e:haxe.Exception) {
			Log.error('guest: uncaught exception in init: ${e.message}');
			return 1;
		}
		return 0;
	}

	static function frameOp(dt:Single, frameId:cpp.UInt32):Int {
		if (onFrame == null)
			return 0;
		try
			onFrame(dt, frameId)
		catch (e:haxe.Exception) {
			Log.error('guest: uncaught exception in frame: ${e.message}');
			return 1;
		}
		return 0;
	}

	static function assetOp(id:cpp.UInt32, path:cpp.ConstCharStar, ok:Int):Int {
		if (onAsset == null)
			return 0;
		try
			onAsset(id, path.toString(), ok != 0)
		catch (e:haxe.Exception) {
			Log.error('guest: uncaught exception in asset: ${e.message}');
			return 1;
		}
		return 0;
	}

	static function shutdownOp():Int {
		if (onShutdown == null)
			return 0;
		try
			onShutdown()
		catch (e:haxe.Exception) {
			Log.error('guest: uncaught exception in shutdown: ${e.message}');
			return 1;
		}
		return 0;
	}

	/**
		Refuses on a version mismatch rather than starting: unlike a frame fault, it is
		known before anything runs, it is total — every call may be wrong — and there is
		no recovering from it. Starting anyway would turn one clear error into a pile of
		confusing ones. (librl's BOOT_ERR_VERSION_MISMATCH does the same.)

		Checked here because it is the one point both routes pass through.
	**/
	public static function start(width:Int, height:Int, title:String, ?flags:WindowFlag):Bool {
		if (!Version.check())
			return false;
		GuestRaw.wgr_guest_start(width, height, title, flags == null ? 0 : (flags : Int));
		return true;
	}

	/**
		Make a file local; the host calls the asset op with `id` when it is.

		`fetchUrl` overrides where the bytes are downloaded from, without changing the
		key they are cached and resolved under -- a mirror, a CDN, a signed link. A
		relative one is read against the asset host, as `Asset.ensureAsync` says.
		`flags` is wgrender's, `ForceFetch` being the one worth knowing: fetch even if
		the cache already has it.
	**/
	public static function loadAsset(path:String, id:Int, ?fetchUrl:String, ?flags:AssetFlag):Bool {
		#if !emscripten
		Asset.needsFetcher(fetchUrl); // an http(s) source natively wants a downloader
		#end
		return GuestRaw.wgr_guest_asset_load(path, id, Native.cstr(fetchUrl),
			flags == null ? 0 : (flags : Int)) != 0;
	}

	/** On desktop there is no page: `main` is the entry, so run the guest now. **/
	public static function autostart(boot:(host:Dynamic) -> Void):Void
		boot(null);
}
