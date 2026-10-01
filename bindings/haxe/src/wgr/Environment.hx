package wgr;

// wgr_environment.h — the environment map resource

/**
	An environment map: lights a scene's PBR models with the world around them, and
	can stand behind them as a skybox. Reference counted and shared, like any resource.

	Load an equirectangular (2:1) image — a Radiance `.hdr` for true high dynamic
	range, or a PNG/JPEG for low. Creating one prepares its lighting on the CPU,
	which takes a fraction of a second for a 1K image, so create environments while
	loading rather than mid-frame.

	Use with `Scene.setEnvironment`, `Scene.setBackground` and `Scene.setTonemap`.
**/
@:using(wgr.Environment)
abstract Environment(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(environment:Environment):Bool
		return (environment : Handle).isNone;

	/** `new Environment(...)` is `Environment.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	public static inline function create(path:String):Environment
		return (Raw.wgr_environment_create(path) : Handle);

	/** Drop this reference; the data goes when the last one does. **/
	public static inline function release(environment:Environment):Void
		Raw.wgr_environment_release(environment);
}
