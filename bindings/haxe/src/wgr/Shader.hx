package wgr;

// wgr_shader.h — a custom material shader resource

/**
	A custom material shader, loaded on create and released like any resource
	(`Resource`): `shader.getStatus()`, `shader.release()`.

	Write a fragment shader (and optionally a vertex hook) against wgrender's
	`shaders/wgr.glsl`, compile it for every backend with `tools/pack_shader.py
	name.glsl`, and load the `.wgrshader` it writes. Use it with `Material.custom`;
	its parameters and textures are then set by the names your shader gives them.
**/
@:using(wgr.Shader, wgr.Resource)
abstract Shader(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(shader:Shader):Bool
		return (shader : Handle).isNone;

	/** `new Shader(...)` is `Shader.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	/**
		The shader at an asset path, loading on create: `Pending` at once, then `Ready`,
		or `Failed` in a later frame for a file that is missing, fails to download, isn't
		a `.wgrshader` of this version or that the backend refuses; `Failed` at once for
		a path outside the asset root or before the asset layer runs. A custom material
		takes it in any status (`Material.custom` says what it does until it's `Ready`).
	**/
	public static inline function create(path:String):Shader
		return (Raw.wgr_shader_create(path) : Handle);
}
