package wgr;

// wgr_shape3d.h — primitives in the world, drawn now or kept in a scene

/**
	A shape, either way round.

	The statics draw immediately, between `Render.beginMode3D` and `endMode3D`, in call
	order. An instance is a scene object instead: give it a form once, place it, and
	add it to a `Scene`. Picked by its surface — the inside of a circle counts, a line
	or strip has no area and is never hit.
**/
@:using(wgr.Shape3D)
abstract Shape3D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(shape3D:Shape3D):Bool
		return (shape3D : Handle).isNone;

	/** `new Shape3D()` is `Shape3D.create()`, the same call. **/
	public inline function new()
		this = create();

	public static inline function create():Shape3D
		return (Raw.wgr_shape3d_create() : Handle);

	// --- the form it takes; the last one set wins ---

	public static inline function setCube(shape3D:Shape3D, size:Vec3):Bool
		return Raw.wgr_shape3d_set_cube(shape3D, size.x, size.y, size.z);

	public static inline function setSphere(shape3D:Shape3D, radius:Float):Bool
		return Raw.wgr_shape3d_set_sphere(shape3D, radius);

	/** Filled. **/
	public static inline function setRectangle(shape3D:Shape3D, width:Float, height:Float):Bool
		return Raw.wgr_shape3d_set_rectangle(shape3D, width, height);

	/** An outline. **/
	public static inline function setCircle(shape3D:Shape3D, radius:Float):Bool
		return Raw.wgr_shape3d_set_circle(shape3D, radius);

	public static inline function setLine(shape3D:Shape3D, from:Vec3, to:Vec3):Bool
		return Raw.wgr_shape3d_set_line(shape3D, from.x, from.y, from.z, to.x, to.y, to.z);

	/** Empty the strip; `addPoint` appends to it. Rebuild it the same way to change it. **/
	public static inline function setLineStrip(shape3D:Shape3D):Bool
		return Raw.wgr_shape3d_set_line_strip(shape3D);

	/** Append a point to the strip, in local space. **/
	public static inline function addPoint(shape3D:Shape3D, point:Vec3):Bool
		return Raw.wgr_shape3d_add_point(shape3D, point.x, point.y, point.z);

	/** Points in the current strip. **/
	public static inline function getPointCount(shape3D:Shape3D):Int
		return Raw.wgr_shape3d_get_point_count(shape3D);

	// --- where it is, and how it looks ---

	/** Position, rotation (radians) and scale in one call: the cheapest way to move it every frame. **/
	public static inline function setTransform(shape3D:Shape3D, position:Vec3, rotation:Vec3, scale:Vec3):Bool
		return Raw.wgr_shape3d_set_transform(shape3D, position.x, position.y, position.z, rotation.x, rotation.y, rotation.z, scale.x,
			scale.y, scale.z);

	/** One part of the transform, leaving the others as they are. **/
	public static overload extern inline function setPosition(shape3D:Shape3D, value:Vec3):Bool
		return setPositionRaw(shape3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setPosition(shape3D:Shape3D, x:Float, y:Float, z:Float):Bool
		return setPositionRaw(shape3D, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setPositionRaw(shape3D:Shape3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_shape3d_set_position(shape3D, x, y, z);

	/** Radians. **/
	public static overload extern inline function setRotation(shape3D:Shape3D, value:Vec3):Bool
		return setRotationRaw(shape3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setRotation(shape3D:Shape3D, x:Float, y:Float, z:Float):Bool
		return setRotationRaw(shape3D, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setRotationRaw(shape3D:Shape3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_shape3d_set_rotation(shape3D, x, y, z);

	public static overload extern inline function setScale(shape3D:Shape3D, value:Vec3):Bool
		return setScaleRaw(shape3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setScale(shape3D:Shape3D, x:Float, y:Float, z:Float):Bool
		return setScaleRaw(shape3D, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setScaleRaw(shape3D:Shape3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_shape3d_set_scale(shape3D, x, y, z);

	/** Where it is, as last set. **/
	public static inline function getPosition(shape3D:Shape3D):Vec3
		return Vec3.of(Raw.wgr_shape3d_get_position(shape3D));

	/** Radians, as last set. **/
	public static inline function getRotation(shape3D:Shape3D):Vec3
		return Vec3.of(Raw.wgr_shape3d_get_rotation(shape3D));

	public static inline function getScale(shape3D:Shape3D):Vec3
		return Vec3.of(Raw.wgr_shape3d_get_scale(shape3D));

	public static inline function setColor(shape3D:Shape3D, value:Color):Bool
		return Raw.wgr_shape3d_set_color(shape3D, value);

	public static inline function isVisible(shape3D:Shape3D):Bool
		return Raw.wgr_shape3d_is_visible(shape3D);

	public static inline function setVisible(shape3D:Shape3D, value:Bool):Bool
		return Raw.wgr_shape3d_set_visible(shape3D, value);

	/** Whether a pick can hit it. Lines and strips have no area either way. **/
	public static inline function isPickable(shape3D:Shape3D):Bool
		return Raw.wgr_shape3d_is_pickable(shape3D);

	/** Whether a pick can hit it. Lines and strips have no area either way. **/
	public static inline function setPickable(shape3D:Shape3D, value:Bool):Bool
		return Raw.wgr_shape3d_set_pickable(shape3D, value);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function isEnabled(shape3D:Shape3D):Bool
		return Raw.wgr_shape3d_is_enabled(shape3D);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function setEnabled(shape3D:Shape3D, value:Bool):Bool
		return Raw.wgr_shape3d_set_enabled(shape3D, value);

	/** Draw it now, inside 3D mode; a scene draws its members itself. **/
	public static inline function draw(shape3D:Shape3D):Void
		Raw.wgr_shape3d_draw(shape3D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(shape3D:Shape3D):Void
		Raw.wgr_shape3d_destroy(shape3D);

	// --- immediate: no handle, drawn where they are called ---

	/** A ground grid of `slices` squares each way, `spacing` world units apart. **/
	public static inline function drawGrid(slices:Int, spacing:Float, color:Color):Void
		Raw.wgr_shape3d_draw_grid(slices, spacing, color);

	public static inline function drawLine(from:Vec3, to:Vec3, color:Color):Void
		Raw.wgr_shape3d_draw_line(from.x, from.y, from.z, to.x, to.y, to.z, color);

	public static inline function drawCube(center:Vec3, size:Vec3, color:Color):Void
		Raw.wgr_shape3d_draw_cube(center.x, center.y, center.z, size.x, size.y, size.z, color);

	public static inline function drawCubeWires(center:Vec3, size:Vec3, color:Color):Void
		Raw.wgr_shape3d_draw_cube_wires(center.x, center.y, center.z, size.x, size.y, size.z, color);

	public static inline function drawSphere(center:Vec3, radius:Float, color:Color):Void
		Raw.wgr_shape3d_draw_sphere(center.x, center.y, center.z, radius, color);

	/** Filled, facing +Z before `rotation` (radians) turns it. **/
	public static inline function drawRectangle(center:Vec3, width:Float, height:Float, rotation:Vec3, color:Color):Void
		Raw.wgr_shape3d_draw_rectangle(center.x, center.y, center.z, width, height, rotation.x, rotation.y, rotation.z, color);

	/** An outline, facing +Z before `rotation` (radians) turns it. **/
	public static inline function drawCircle(center:Vec3, radius:Float, rotation:Vec3, color:Color):Void
		Raw.wgr_shape3d_draw_circle(center.x, center.y, center.z, radius, rotation.x, rotation.y, rotation.z, color);
}
