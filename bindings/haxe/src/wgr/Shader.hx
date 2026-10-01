package wgr;

// wgr_shader.h — a custom material shader resource

/**
	A custom material shader: reference counted and shared, like any resource.

	Write a fragment shader (and optionally a vertex hook) against wgrender's
	`shaders/wgr.glsl`, compile it for every backend with `tools/pack_shader.py
	name.glsl`, and load the `.wgrshader` it writes. Use it with `Material.custom`;
	its parameters and textures are then set by the names your shader gives them.
**/
@:using(wgr.Shader)
abstract Shader(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(shader:Shader):Bool
		return (shader : Handle).isNone;

	/** `new Shader(...)` is `Shader.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	public static inline function create(path:String):Shader
		return (Raw.wgr_shader_create(path) : Handle);

	/** Drop this reference; the data goes when the last one does. **/
	public static inline function release(shader:Shader):Void
		Raw.wgr_shader_release(shader);
}
