package wgr;

// wgr_shape2d.h — primitives on the screen, drawn now or kept in a scene

/**
	The screen-space counterpart of `Shape3D`, and the same two ways round: the statics
	draw immediately in logical pixels with a top-left origin, an instance is a scene
	object with its own transform, pivot and outline.
**/
@:using(wgr.Shape2D)
abstract Shape2D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(shape2D:Shape2D):Bool
		return (shape2D : Handle).isNone;

	/** `new Shape2D()` is `Shape2D.create()`, the same call. **/
	public inline function new()
		this = create();

	public static inline function create():Shape2D
		return (Raw.wgr_shape2d_create() : Handle);

	// --- the form it takes; the last one set wins ---

	public static inline function setRectangle(shape2D:Shape2D, width:Float, height:Float, cornerRadius:Float = 0):Bool
		return Raw.wgr_shape2d_set_rectangle(shape2D, width, height, cornerRadius);

	public static inline function setCircle(shape2D:Shape2D, radius:Float):Bool
		return Raw.wgr_shape2d_set_circle(shape2D, radius);

	public static inline function setLine(shape2D:Shape2D, from:Vec2, to:Vec2, thickness:Float = 1):Bool
		return Raw.wgr_shape2d_set_line(shape2D, from.x, from.y, to.x, to.y, thickness);

	// --- where it is, and how it looks ---

	/** Position, rotation (radians) and scale in one call: the cheapest way to move it every frame. **/
	public static inline function setTransform(shape2D:Shape2D, position:Vec2, rotation:Float, scale:Vec2):Bool
		return Raw.wgr_shape2d_set_transform(shape2D, position.x, position.y, rotation, scale.x, scale.y);

	/** One part of the transform, leaving the others as they are. Where the pivot goes. **/
	public static overload extern inline function setPosition(shape2D:Shape2D, value:Vec2):Bool
		return Raw.wgr_shape2d_set_position(shape2D, value.x, value.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function setPosition(shape2D:Shape2D, x:Float, y:Float):Bool
		return Raw.wgr_shape2d_set_position(shape2D, x, y);

	/** Radians around the pivot; positive turns clockwise, since y points down. **/
	public static inline function setRotation(shape2D:Shape2D, value:Float):Bool
		return Raw.wgr_shape2d_set_rotation(shape2D, value);

	/** Multiplies the size. A negative component flips it on that axis. **/
	public static overload extern inline function setScale(shape2D:Shape2D, value:Vec2):Bool
		return Raw.wgr_shape2d_set_scale(shape2D, value.x, value.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function setScale(shape2D:Shape2D, x:Float, y:Float):Bool
		return Raw.wgr_shape2d_set_scale(shape2D, x, y);

	/** Where the pivot is, as last set. **/
	public static inline function getPosition(shape2D:Shape2D):Vec2
		return Vec2.of(Raw.wgr_shape2d_get_position(shape2D));

	/** Radians, as last set. **/
	public static inline function getRotation(shape2D:Shape2D):Float
		return Raw.wgr_shape2d_get_rotation(shape2D);

	public static inline function getScale(shape2D:Shape2D):Vec2
		return Vec2.of(Raw.wgr_shape2d_get_scale(shape2D));

	/**
		What the transform turns and scales about, as a **fraction of the shape's own
		size** — (0, 0) its top-left, (0.5, 0.5) its middle, (1, 1) its bottom-right.
		Not pixels: (80, 30) puts it eighty widths away. Outside 0..1 is allowed.

		The default is the shape's own origin — a rectangle's top-left, a circle's
		centre — so nothing moves until one is set. Lines have explicit endpoints and
		ignore it.
	**/
	public static overload extern inline function setPivot(shape2D:Shape2D, pivot:Vec2):Bool
		return Raw.wgr_shape2d_set_pivot(shape2D, pivot.x, pivot.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function setPivot(shape2D:Shape2D, x:Float, y:Float):Bool
		return Raw.wgr_shape2d_set_pivot(shape2D, x, y);

	/** The pivot in effect: before `setPivot`, the kind's own origin -- (0, 0) for a rectangle, (0.5, 0.5) for a circle. **/
	public static inline function getPivot(shape2D:Shape2D):Vec2
		return Vec2.of(Raw.wgr_shape2d_get_pivot(shape2D));

	public static inline function setColor(shape2D:Shape2D, value:Color):Bool
		return Raw.wgr_shape2d_set_color(shape2D, value);

	/** Outline thickness; 0 fills, which is the default. Rectangles and circles. **/
	public static inline function setOutline(shape2D:Shape2D, value:Float):Bool
		return Raw.wgr_shape2d_set_outline(shape2D, value);

	public static inline function isVisible(shape2D:Shape2D):Bool
		return Raw.wgr_shape2d_is_visible(shape2D);

	public static inline function setVisible(shape2D:Shape2D, value:Bool):Bool
		return Raw.wgr_shape2d_set_visible(shape2D, value);

	/** Whether a pick can hit it. **/
	public static inline function isPickable(shape2D:Shape2D):Bool
		return Raw.wgr_shape2d_is_pickable(shape2D);

	/** Whether a pick can hit it. **/
	public static inline function setPickable(shape2D:Shape2D, value:Bool):Bool
		return Raw.wgr_shape2d_set_pickable(shape2D, value);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function isEnabled(shape2D:Shape2D):Bool
		return Raw.wgr_shape2d_is_enabled(shape2D);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function setEnabled(shape2D:Shape2D, value:Bool):Bool
		return Raw.wgr_shape2d_set_enabled(shape2D, value);

	/** Draw it now; a scene draws its members itself. **/
	public static inline function draw(shape2D:Shape2D):Void
		Raw.wgr_shape2d_draw(shape2D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(shape2D:Shape2D):Void
		Raw.wgr_shape2d_destroy(shape2D);

	// --- immediate: no handle, drawn where they are called ---

	public static inline function drawRectangle(x:Float, y:Float, width:Float, height:Float, color:Color):Void
		Raw.wgr_shape2d_draw_rectangle(x, y, width, height, color);

	public static inline function drawRectangleLines(x:Float, y:Float, width:Float, height:Float, color:Color):Void
		Raw.wgr_shape2d_draw_rectangle_lines(x, y, width, height, color);

	public static inline function drawLine(from:Vec2, to:Vec2, color:Color):Void
		Raw.wgr_shape2d_draw_line(from.x, from.y, to.x, to.y, color);

	public static inline function drawCircle(center:Vec2, radius:Float, color:Color):Void
		Raw.wgr_shape2d_draw_circle(center.x, center.y, radius, color);

	public static inline function drawCircleLines(center:Vec2, radius:Float, color:Color):Void
		Raw.wgr_shape2d_draw_circle_lines(center.x, center.y, radius, color);

	public static inline function drawTriangle(a:Vec2, b:Vec2, c:Vec2, color:Color):Void
		Raw.wgr_shape2d_draw_triangle(a.x, a.y, b.x, b.y, c.x, c.y, color);

	/** Every corner rounded the same. **/
	public static overload extern inline function drawRoundedRectangle(x:Float, y:Float, width:Float, height:Float,
			radius:Float, color:Color):Void
		Raw.wgr_shape2d_draw_rounded_rectangle(x, y, width, height, radius, radius, radius, radius, color);

	/** Corner radii clockwise from the top left. **/
	public static overload extern inline function drawRoundedRectangle(x:Float, y:Float, width:Float, height:Float,
			topLeft:Float, topRight:Float, bottomRight:Float, bottomLeft:Float, color:Color):Void
		Raw.wgr_shape2d_draw_rounded_rectangle(x, y, width, height, topLeft, topRight, bottomRight, bottomLeft,
			color);

	/** An inset border: per-edge thickness, then corner radii clockwise from top left. **/
	public static inline function drawBorder(x:Float, y:Float, width:Float, height:Float, left:Float, top:Float,
			right:Float, bottom:Float, topLeft:Float, topRight:Float, bottomRight:Float, bottomLeft:Float,
			color:Color):Void
		Raw.wgr_shape2d_draw_border(x, y, width, height, left, top, right, bottom, topLeft, topRight, bottomRight,
			bottomLeft, color);
}
