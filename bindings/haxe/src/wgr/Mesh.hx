package wgr;

// wgr_model.h — the geometry resource

/**
	Model geometry, loaded on create (a glTF or GLB file) or generated, and released
	like any resource (`Resource`): `mesh.getStatus()`, `mesh.release()`.
**/
@:using(wgr.Mesh, wgr.Resource)
abstract Mesh(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(mesh:Mesh):Bool
		return (mesh : Handle).isNone;

	/** `new Mesh(...)` is `Mesh.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	/**
		The glTF or GLB file at an asset path, loading on create: `Pending` at once, then
		`Ready`, or `Failed` in a later frame for a file that is missing, fails to
		download or won't parse, or whose buffers are missing; `Failed` at once for a
		path outside the asset root or before the asset layer runs. The files it names
		load with it (a missing image warns and draws the placeholder). `Handle.NONE`
		only when there's no room for another. A model using it isn't drawn or picked
		until it's `Ready`, and keeps its animation and material overrides for then.
	**/
	public static inline function create(path:String):Mesh
		return (Raw.wgr_mesh_create(path) : Handle);

	/** One slot per glTF material. **/
	public static inline function getMaterialCount(mesh:Mesh):Int
		return Raw.wgr_mesh_get_material_count(mesh);

	/**
		The material in `slot`, borrowed: it stays valid while the mesh lives, and
		changing it changes every model using the mesh. To change one model, give that
		model an override with `Model.setMaterial`.
	**/
	public static inline function getMaterial(mesh:Mesh, slot:Int):Material
		return (Raw.wgr_mesh_get_material(mesh, slot) : Handle);

	// --- generated meshes ---------------------------------------------------
	//
	// Shapes made in code, with normals, both sets of texture coordinates and
	// tangents, so any material lights them — normal maps and custom shaders
	// included. Centered on the origin, y up, in meters. Like a loaded mesh they are
	// resources: deduplicated (the same parameters return the same mesh, with one
	// more reference) and never changed once made. To size one model differently,
	// scale the model rather than making another mesh. One material slot: white, not
	// metallic, roughness 0.5 — replace it with `Model.setMaterial`. A size at or
	// below 0 gives a none handle, and is logged; counts are clamped to their ranges.

	/** Flat in XZ facing +Y. `subdivisions` 0..256 adds that many cells each way. **/
	public static inline function plane(width:Float, length:Float, subdivisions:Int = 0):Mesh
		return (Raw.wgr_mesh_create_plane(width, length, subdivisions) : Handle);

	/** Each face its own vertices, so the edges stay sharp; textured 0..1 per face. **/
	public static inline function cube(width:Float, height:Float, length:Float):Mesh
		return (Raw.wgr_mesh_create_cube(width, height, length) : Handle);

	/** `rings` 2..256 pole to pole, `segments` 3..512 around. **/
	public static inline function sphere(radius:Float, rings:Int = 16, segments:Int = 32):Mesh
		return (Raw.wgr_mesh_create_sphere(radius, rings, segments) : Handle);

	/** Capped; `segments` 3..512 around. **/
	public static inline function cylinder(radius:Float, height:Float, segments:Int = 32):Mesh
		return (Raw.wgr_mesh_create_cylinder(radius, height, segments) : Handle);

	/** Tip up, capped base. **/
	public static inline function cone(radius:Float, height:Float, segments:Int = 32):Mesh
		return (Raw.wgr_mesh_create_cone(radius, height, segments) : Handle);

	/** `height` is end to end, at least twice `radius`; less than that gives a sphere. **/
	public static inline function capsule(radius:Float, height:Float, rings:Int = 8, segments:Int = 32):Mesh
		return (Raw.wgr_mesh_create_capsule(radius, height, rings, segments) : Handle);

	/** Around y. `radius` reaches the middle of the tube, `thickness` is its radius. **/
	public static inline function torus(radius:Float, thickness:Float, rings:Int = 16, segments:Int = 32):Mesh
		return (Raw.wgr_mesh_create_torus(radius, thickness, rings, segments) : Handle);
}
