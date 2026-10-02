package wgr;


/**
	The guest side of `host/wgr_guest.h`, for the JS target: the host is an Emscripten
	module and the ops are JS functions turned into C function pointers.

	Haxe picks this over `GuestAbi.cpp.hx` by target, so `Guest.hx` carries no `#if` of
	its own — the whole point of doing this in Haxe rather than twice.

	Each op wrapper owns two things the C side can't do for us:

	- **it catches**, converting a throw into the nonzero return the ABI expects,
	  because an exception escaping into the host's frames freezes the page on an
	  opaque `Uncaught [object WebAssembly.Exception]`;
	- **it releases the scratch arena** — `Raw` allocates strings and struct-return
	  slots on the wasm stack, and Haxe has no `finally`, so the op edge restores the
	  stack pointer once, fault or not.
**/
class GuestAbi {
	/** The Emscripten module every call goes through. **/
	public static function attach(host:Dynamic):Void
		Raw.attach(host);

	static var onInit:() -> Void;
	static var onFrame:(dt:Float, frameId:Int) -> Void;
	static var onTick:(dt:Float) -> Void;
	static var onShutdown:() -> Void;

	/**
		Declare the ops, all of them at once.

		Every slot is written, including `shutdown` when it is left out — so this
		*clears* a handler set earlier by `setShutdown` or `wgr.Wgr.setShutdown`. That
		is what "these are my ops" means, and it is the reason to reach for the
		single-op setters instead when changing one and leaving the rest alone.

		`shutdown` is optional and runs once, after the last frame, on the way out --
		wgrender's `wgr_set_shutdown` for a guest. It is the op to release anything the
		guest owns outside wgrender; anything wgrender owns is already being torn down.
	**/
	public static function register(init:() -> Void, frame:(dt:Float, frameId:Int) -> Void, ?shutdown:() -> Void):Void {
		onInit = init;
		onFrame = frame;
		onShutdown = shutdown;
		installOps();
	}

	static var opsInstalled = false;

	/**
		Hand wgrender the dispatchers, once. They read `onInit` and the rest when they
		fire, so this never has to run again however often a handler changes -- which
		matters on js, where every `op` is a wasm table entry that cannot be taken back.
	**/
	static function ensureInstalled():Void {
		if (!opsInstalled)
			installOps();
	}

	static function installOps():Void {
		opsInstalled = true;
		Raw.host._wgr_guest_register(op("init", "i", () -> callInit()),
			op("frame", "ifi", (dt:Float, frameId:Int) -> callFrame(dt, frameId)),
			op("shutdown", "i", () -> callShutdown()));
		Raw.host._wgr_guest_install();
	}

	// Null-tolerant, because a program may set only the ops it cares about.
	/**
		Put the ops back after something wiped them.

		`wgr_init_values` memsets wgrender's runtime, so a handler set before it is
		gone afterwards. `wgr.Wgr.initValues` calls this on the way out. Only reapplies
		what was actually registered: a program that never used the ops gets nothing
		installed, and keeps whatever it set itself.
	**/
	public static function reinstall():Void {
		if (opsInstalled)
			Raw.wgr_guest_install();
	}

	static function callInit():Void if (onInit != null) onInit();

	static function callFrame(dt:Float, frameId:Int):Void {
		Asset.takeFetches(); // the downloads wgrender asked for since the last frame (setFetcher)
		if (onFrame != null)
			onFrame(dt, frameId);
	}

	static function callShutdown():Void if (onShutdown != null) onShutdown();

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


	/**
		Fixed-rate simulation: `tick` runs 0..N times before each frame, always with
		`dt` of 1/`hz`. Optional, and set on its own because it is the one op that
		needs configuring. `hz` of 0 or less turns it off.

		Draw in the frame op, not here, and use `tickFraction` there to interpolate
		between the last two tick states.
	**/
	public static function registerTick(tick:(dt:Float) -> Void, hz:Int):Void {
		onTick = tick;
		Raw.host._wgr_guest_register_tick(op("tick", "if", (dt:Float) -> onTick(dt)), hz);
	}

	/** How far this frame is into the next tick, 0..1. 0 without a tick. **/
	public static inline function tickFraction():Float
		return Raw.host._wgr_guest_tick_fraction();

	static function op(name:String, signature:String, body:Dynamic):Int {
		return Raw.host.addFunction(Reflect.makeVarArgs(args -> {
			final mark = Raw.stackMark();
			var code = 0;
			try
				Reflect.callMethod(null, body, args)
			catch (e:haxe.Exception) {
				Log.error('guest: uncaught exception in $name: ${e.message}');
				code = 1;
			}
			Raw.stackRelease(mark);
			return code;
		}), signature);
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
		final mark = Raw.stackMark();
		Raw.host._wgr_guest_start(width, height, Raw.cstr(title), flags == null ? 0 : (flags : Int));
		Raw.stackRelease(mark);
		return true;
	}

	/** On js the page owns startup: web/boot.js loads the host, then calls `start`. **/
	public static function autostart(_:(host:Dynamic) -> Void):Void {}
}
