package wgr;

// wgr_sprite2d.h — a Texture placed on the screen

/**
	A textured quad in screen space, referencing a shared `Texture`.

	Coordinates are logical pixels with a top-left origin and y down, so on a
	high-DPI display one logical pixel spans several real ones. Angles are radians,
	and positive turns clockwise on screen because y points down.

	Add it to a `Scene` — which draws all 3D first, then its 2D members by layer and
	insertion order, and picks 2D members first, topmost first — or `draw` it
	directly, in call order. A sprite with no texture, or one still loading, is
	neither drawn nor picked.
**/
@:using(wgr.Sprite2D)
abstract Sprite2D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(sprite2D:Sprite2D):Bool
		return (sprite2D : Handle).isNone;

	/** `new Sprite2D(...)` is `Sprite2D.create(...)`, the same call. **/
	public inline function new(texture:Texture)
		this = create(texture);

	/** `texture` may be none and set later with `setTexture`. **/
	public static inline function create(texture:Texture):Sprite2D
		return (Raw.wgr_sprite2d_create(texture) : Handle);

	/** Position, rotation (radians) and scale in one call: the cheapest way to move it every frame. **/
	public static inline function setTransform(sprite2D:Sprite2D, position:Vec2, rotation:Float, scale:Vec2):Bool
		return Raw.wgr_sprite2d_set_transform(sprite2D, position.x, position.y, rotation, scale.x, scale.y);

	/** One part of the transform, leaving the others as they are. Where the pivot goes, in logical pixels. **/
	public static overload extern inline function setPosition(sprite2D:Sprite2D, value:Vec2):Bool
		return Raw.wgr_sprite2d_set_position(sprite2D, value.x, value.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function setPosition(sprite2D:Sprite2D, x:Float, y:Float):Bool
		return Raw.wgr_sprite2d_set_position(sprite2D, x, y);

	/** Radians around the pivot; positive turns clockwise, since y points down. **/
	public static inline function setRotation(sprite2D:Sprite2D, value:Float):Bool
		return Raw.wgr_sprite2d_set_rotation(sprite2D, value);

	/** Multiplies the size. A negative component flips it on that axis. **/
	public static overload extern inline function setScale(sprite2D:Sprite2D, value:Vec2):Bool
		return Raw.wgr_sprite2d_set_scale(sprite2D, value.x, value.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function setScale(sprite2D:Sprite2D, x:Float, y:Float):Bool
		return Raw.wgr_sprite2d_set_scale(sprite2D, x, y);

	/** Where the pivot is, as last set. **/
	public static inline function getPosition(sprite2D:Sprite2D):Vec2
		return Vec2.of(Raw.wgr_sprite2d_get_position(sprite2D));

	/** Radians, as last set. **/
	public static inline function getRotation(sprite2D:Sprite2D):Float
		return Raw.wgr_sprite2d_get_rotation(sprite2D);

	public static inline function getScale(sprite2D:Sprite2D):Vec2
		return Vec2.of(Raw.wgr_sprite2d_get_scale(sprite2D));

	public static inline function setTint(sprite2D:Sprite2D, value:Color):Bool
		return Raw.wgr_sprite2d_set_tint(sprite2D, value);

	/** Drawn at all. **/
	public static inline function isVisible(sprite2D:Sprite2D):Bool
		return Raw.wgr_sprite2d_is_visible(sprite2D);

	/** Drawn at all. **/
	public static inline function setVisible(sprite2D:Sprite2D, value:Bool):Bool
		return Raw.wgr_sprite2d_set_visible(sprite2D, value);

	/** Whether a pick can hit it. Default: it can. **/
	public static inline function isPickable(sprite2D:Sprite2D):Bool
		return Raw.wgr_sprite2d_is_pickable(sprite2D);

	/** Whether a pick can hit it. Default: it can. **/
	public static inline function setPickable(sprite2D:Sprite2D, value:Bool):Bool
		return Raw.wgr_sprite2d_set_pickable(sprite2D, value);

	/**
		Enabled (the default): a hit reacts — hover, press and click in an interactive
		scene. Disabled: still drawn and still picked, and it still blocks the pointer,
		but it doesn't react.
	**/
	public static inline function isEnabled(sprite2D:Sprite2D):Bool
		return Raw.wgr_sprite2d_is_enabled(sprite2D);

	/**
		Enabled (the default): a hit reacts — hover, press and click in an interactive
		scene. Disabled: still drawn and still picked, and it still blocks the pointer,
		but it doesn't react.
	**/
	public static inline function setEnabled(sprite2D:Sprite2D, value:Bool):Bool
		return Raw.wgr_sprite2d_set_enabled(sprite2D, value);

	/** How it uses its texture's alpha. Set it with `setAlphaMode`. **/
	public static inline function getAlphaMode(sprite2D:Sprite2D):AlphaMode
		return AlphaMode.of(Raw.wgr_sprite2d_get_alpha_mode(sprite2D));

	/** The sprite takes its own reference; a none texture leaves it nothing to draw. **/
	public static inline function setTexture(sprite2D:Sprite2D, texture:Texture):Bool
		return Raw.wgr_sprite2d_set_texture(sprite2D, texture);

	/**
		The region of the texture to show, in texture pixels — for sprite sheets and
		atlases. A width or height at or below 0 goes back to the whole texture.
	**/
	public static inline function setSource(sprite2D:Sprite2D, x:Float, y:Float, width:Float, height:Float):Bool
		return Raw.wgr_sprite2d_set_source(sprite2D, x, y, width, height);

	/**
		Its on-screen size in logical pixels, before scale. A width or height at or
		below 0 means the source region's own size, which is the default.
	**/
	public static inline function setSize(sprite2D:Sprite2D, width:Float, height:Float):Bool
		return Raw.wgr_sprite2d_set_size(sprite2D, width, height);

	/**
		The point `position` refers to and `rotation` turns around, **as a fraction of
		the sprite** rather than in pixels: (0, 0) is its top-left and (1, 1) its
		bottom-right. Default (0.5, 0.5), the center.
	**/
	public static overload extern inline function setPivot(sprite2D:Sprite2D, x:Float, y:Float):Bool
		return Raw.wgr_sprite2d_set_pivot(sprite2D, x, y);

	/** The same, as a `Vec2`. **/
	public static overload extern inline function setPivot(sprite2D:Sprite2D, pivot:Vec2):Bool
		return Raw.wgr_sprite2d_set_pivot(sprite2D, pivot.x, pivot.y);

	/** The pivot, as set; (0.5, 0.5) by default. **/
	public static inline function getPivot(sprite2D:Sprite2D):Vec2
		return Vec2.of(Raw.wgr_sprite2d_get_pivot(sprite2D));

	/**
		Nine-slice: borders in source pixels that keep their size when the sprite is
		drawn at another size — panels, buttons, frames. The corners stay put, the edges
		stretch along one axis and the middle along both. A 0 border leaves that axis
		unsliced and all four at 0 turns it off, which is the default. Set the on-screen
		size with `setSize`. Picks hit the whole rectangle: the alpha test is skipped
		while a sprite is sliced.
	**/
	public static inline function setNineSlice(sprite2D:Sprite2D, left:Float, top:Float, right:Float, bottom:Float):Bool
		return Raw.wgr_sprite2d_set_nine_slice(sprite2D, left, top, right, bottom);

	/**
		How it uses its texture's alpha; `Blend` by default. Blended, `Add` for glows,
		`Opaque` to ignore alpha, or `Mask` to cut out texels below `cutoff` (0..1). 2D
		sprites always draw in order — the mode only changes how they are blended.
	**/
	public static inline function setAlphaMode(sprite2D:Sprite2D, mode:AlphaMode, cutoff:Float = 0):Bool
		return Raw.wgr_sprite2d_set_alpha_mode(sprite2D, mode, cutoff);

	/**
		Draw it with a material instead of wgrender's sprite shader; a none material
		goes back to that. A custom material's shader draws the sprite in screen pixels
		— `wgr_world_pos` is the pixel, and the scene's lights don't reach 2D. On a
		nine-sliced sprite, `wgr_uv1` spans each slice.
	**/
	public static inline function setMaterial(sprite2D:Sprite2D, material:Material):Bool
		return Raw.wgr_sprite2d_set_material(sprite2D, material);

	/** What it draws with, borrowed; none when it's on the built-in sprite shader. **/
	public static inline function getMaterial(sprite2D:Sprite2D):Material
		return (Raw.wgr_sprite2d_get_material(sprite2D) : Handle);

	/** Let picks pass through texels whose alpha is below `threshold` (0..1). **/
	public static inline function setPickAlphaTest(sprite2D:Sprite2D, enable:Bool, threshold:Float = 0.5):Bool
		return Raw.wgr_sprite2d_set_pick_alpha_test(sprite2D, enable, threshold);

	/** Draw it once, now, outside any scene, in call order. **/
	public static inline function draw(sprite2D:Sprite2D):Void
		Raw.wgr_sprite2d_draw(sprite2D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(sprite2D:Sprite2D):Void
		Raw.wgr_sprite2d_destroy(sprite2D);
}
