package wgr;

// wgr_environment.h — the environment map resource

/**
	An environment map: lights a scene's PBR models with the world around them, and
	can stand behind them as a skybox. Loaded on create and released like any resource
	(`Resource`): `environment.getStatus()`, `environment.release()`.

	Load an equirectangular (2:1) image — a Radiance `.hdr` for true high dynamic
	range, or a PNG/JPEG for low. Its lighting is prepared on a worker thread (where
	there are threads), a fraction of a second for a 1K image, while the frames go on.

	Use with `Scene.setEnvironment`, `Scene.setBackground` and `Scene.setTonemap`.
**/
@:using(wgr.Environment, wgr.Resource)
abstract Environment(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(environment:Environment):Bool
		return (environment : Handle).isNone;

	/** `new Environment(...)` is `Environment.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	/**
		The environment at an asset path, loading on create: `Pending` at once, then
		`Ready`, or `Failed` in a later frame for a file that is missing, fails to
		download or won't decode. `Failed` at once for a path outside the asset root,
		before the asset layer runs, or on a backend that can't filter half floats.
		`Handle.NONE` only when there's no room for another. Until it's `Ready` a scene
		using it is lit as if it had none and draws no background.
	**/
	public static inline function create(path:String):Environment
		return (Raw.wgr_environment_create(path) : Handle);
}
