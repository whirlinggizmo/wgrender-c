package wgr;

// wgr_text3d.h — retained text in the world

/**
	The same, placed in the 3D world instead of on the screen: `size` is in world
	units, it has a transform and a `facing`, and it is depth-tested and sorted with
	the scene's other transparent parts. Note there is no scale — the size is the
	scale.
**/
@:using(wgr.Text3D)
abstract Text3D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(text3D:Text3D):Bool
		return (text3D : Handle).isNone;

	/** `new Text3D(...)` is `Text3D.create(...)`, the same call. **/
	public inline function new(font:Font)
		this = create(font);

	/** `Handle.NONE` for the default font; attach a real one later with `font`. **/
	public static inline function create(font:Font):Text3D
		return (Raw.wgr_text3d_create(font) : Handle);

	public static inline function setFont(text3D:Text3D, value:Font):Bool
		return Raw.wgr_text3d_set_font(text3D, value);

	public static inline function setText(text3D:Text3D, value:String):Bool
		return Raw.wgr_text3d_set_text(text3D, value); // wgrender copies it;

	/** Line height in world units (default 1), descender to ascender. **/
	public static inline function setSize(text3D:Text3D, value:Float):Bool
		return Raw.wgr_text3d_set_size(text3D, value);

	public static inline function setColor(text3D:Text3D, value:Color):Bool
		return Raw.wgr_text3d_set_color(text3D, value);

	/** Wrap to this many world units, between words; 0 is off (the default). **/
	public static inline function setMaxWidth(text3D:Text3D, value:Float):Bool
		return Raw.wgr_text3d_set_max_width(text3D, value);

	public static inline function setFacing(text3D:Text3D, value:SpriteFacing):Bool
		return Raw.wgr_text3d_set_facing(text3D, value);

	public static inline function isVisible(text3D:Text3D):Bool
		return Raw.wgr_text3d_is_visible(text3D);

	public static inline function setVisible(text3D:Text3D, value:Bool):Bool
		return Raw.wgr_text3d_set_visible(text3D, value);

	/** Whether a pick can hit it. Default: pickable. **/
	public static inline function isPickable(text3D:Text3D):Bool
		return Raw.wgr_text3d_is_pickable(text3D);

	/** Whether a pick can hit it. Default: pickable. **/
	public static inline function setPickable(text3D:Text3D, value:Bool):Bool
		return Raw.wgr_text3d_set_pickable(text3D, value);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function isEnabled(text3D:Text3D):Bool
		return Raw.wgr_text3d_is_enabled(text3D);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function setEnabled(text3D:Text3D, value:Bool):Bool
		return Raw.wgr_text3d_set_enabled(text3D, value);

	/** Default: centred both ways, so the position is the middle of the block. **/
	public static inline function setAlign(text3D:Text3D, horizontal:AlignX, vertical:AlignY):Bool
		return Raw.wgr_text3d_set_align(text3D, horizontal, vertical);

	/** Position and rotation (radians) in one call. No scale: `size` is the scale. **/
	public static inline function setTransform(text3D:Text3D, position:Vec3, rotation:Vec3):Bool
		return Raw.wgr_text3d_set_transform(text3D, position.x, position.y, position.z, rotation.x, rotation.y, rotation.z);

	/** One part of the transform, leaving the others as they are. **/
	public static overload extern inline function setPosition(text3D:Text3D, value:Vec3):Bool
		return Raw.wgr_text3d_set_position(text3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setPosition(text3D:Text3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_text3d_set_position(text3D, x, y, z);

	/** Radians. **/
	public static overload extern inline function setRotation(text3D:Text3D, value:Vec3):Bool
		return Raw.wgr_text3d_set_rotation(text3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setRotation(text3D:Text3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_text3d_set_rotation(text3D, x, y, z);

	/** Where it is, as last set. **/
	public static inline function getPosition(text3D:Text3D):Vec3
		return Vec3.of(Raw.wgr_text3d_get_position(text3D));

	/** Radians, as last set. **/
	public static inline function getRotation(text3D:Text3D):Vec3
		return Vec3.of(Raw.wgr_text3d_get_rotation(text3D));

	/** World-space width and height of the current text; (0, 0) until the font loads. **/
	public static inline function measure(text3D:Text3D):Vec2
		return Vec2.of(Raw.wgr_text3d_get_size(text3D));

	/** Draw it now, inside 3D mode; a scene draws its members itself. **/
	public static inline function draw(text3D:Text3D):Void
		Raw.wgr_text3d_draw(text3D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(text3D:Text3D):Void
		Raw.wgr_text3d_destroy(text3D);
}
