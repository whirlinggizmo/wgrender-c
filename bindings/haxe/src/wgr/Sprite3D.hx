package wgr;

// wgr_sprite3d.h — a Texture placed in the 3D scene

/** A `Texture` placed in the 3D scene, with its own transform, tint and facing. **/
@:using(wgr.Sprite3D)
abstract Sprite3D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(sprite3D:Sprite3D):Bool
		return (sprite3D : Handle).isNone;

	/** `new Sprite3D(...)` is `Sprite3D.create(...)`, the same call. **/
	public inline function new(texture:Texture)
		this = create(texture);

	/** The sprite takes its own reference to `texture`. **/
	public static inline function create(texture:Texture):Sprite3D
		return (Raw.wgr_sprite3d_create(texture) : Handle);

	/** Where it is, as last set. **/
	public static inline function getPosition(sprite3D:Sprite3D, ?result:Vec3):Vec3
		return Vec3.of(Raw.wgr_sprite3d_get_position(sprite3D), result);

	/** Its own rotation in radians — what `Free` facing uses. **/
	public static inline function getRotation(sprite3D:Sprite3D, ?result:Vec3):Vec3
		return Vec3.of(Raw.wgr_sprite3d_get_rotation(sprite3D), result);

	public static inline function getScale(sprite3D:Sprite3D, ?result:Vec3):Vec3
		return Vec3.of(Raw.wgr_sprite3d_get_scale(sprite3D), result);

	/** Drawn at all. **/
	public static inline function isVisible(sprite3D:Sprite3D):Bool
		return Raw.wgr_sprite3d_is_visible(sprite3D);

	/** Drawn at all. **/
	public static inline function setVisible(sprite3D:Sprite3D, value:Bool):Bool
		return Raw.wgr_sprite3d_set_visible(sprite3D, value);

	/** Whether a pick can hit it. Default: it can. **/
	public static inline function isPickable(sprite3D:Sprite3D):Bool
		return Raw.wgr_sprite3d_is_pickable(sprite3D);

	/** Whether a pick can hit it. Default: it can. **/
	public static inline function setPickable(sprite3D:Sprite3D, value:Bool):Bool
		return Raw.wgr_sprite3d_set_pickable(sprite3D, value);

	/**
		Enabled (the default): a hit reacts — hover, press and click in an interactive
		scene. Disabled: still drawn and still picked, and it still blocks the pointer,
		but it doesn't react.
	**/
	public static inline function isEnabled(sprite3D:Sprite3D):Bool
		return Raw.wgr_sprite3d_is_enabled(sprite3D);

	/**
		Enabled (the default): a hit reacts — hover, press and click in an interactive
		scene. Disabled: still drawn and still picked, and it still blocks the pointer,
		but it doesn't react.
	**/
	public static inline function setEnabled(sprite3D:Sprite3D, value:Bool):Bool
		return Raw.wgr_sprite3d_set_enabled(sprite3D, value);

	/** How it uses its texture's alpha. Set it with `setAlphaMode`. **/
	public static inline function getAlphaMode(sprite3D:Sprite3D):AlphaMode
		return AlphaMode.of(Raw.wgr_sprite3d_get_alpha_mode(sprite3D));

	/**
		The world size of the quad before scale, square. The rectangular form is
		`setExtent`. Default 1x1; a size at or below 0 is refused.
	**/
	public static inline function setSize(sprite3D:Sprite3D, value:Float):Bool
		return Raw.wgr_sprite3d_set_size(sprite3D, value);

	public static inline function setFacing(sprite3D:Sprite3D, value:SpriteFacing):Bool
		return Raw.wgr_sprite3d_set_facing(sprite3D, value);

	public static inline function setTint(sprite3D:Sprite3D, value:Color):Bool
		return Raw.wgr_sprite3d_set_tint(sprite3D, value);

	/** Position, rotation (radians) and scale in one call: the cheapest way to move it every frame. **/
	public static inline function setTransform(sprite3D:Sprite3D, position:Vec3, rotation:Vec3, scale:Vec3):Bool
		return Raw.wgr_sprite3d_set_transform(sprite3D, position.x, position.y, position.z, rotation.x, rotation.y, rotation.z, scale.x,
			scale.y, scale.z);

	/** One part of the transform, leaving the others as they are. **/
	public static overload extern inline function setPosition(sprite3D:Sprite3D, value:Vec3):Bool
		return setPositionRaw(sprite3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setPosition(sprite3D:Sprite3D, x:Float, y:Float, z:Float):Bool
		return setPositionRaw(sprite3D, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setPositionRaw(sprite3D:Sprite3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_sprite3d_set_position(sprite3D, x, y, z);

	/** Radians. **/
	public static overload extern inline function setRotation(sprite3D:Sprite3D, value:Vec3):Bool
		return setRotationRaw(sprite3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setRotation(sprite3D:Sprite3D, x:Float, y:Float, z:Float):Bool
		return setRotationRaw(sprite3D, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setRotationRaw(sprite3D:Sprite3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_sprite3d_set_rotation(sprite3D, x, y, z);

	public static overload extern inline function setScale(sprite3D:Sprite3D, value:Vec3):Bool
		return setScaleRaw(sprite3D, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setScale(sprite3D:Sprite3D, x:Float, y:Float, z:Float):Bool
		return setScaleRaw(sprite3D, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setScaleRaw(sprite3D:Sprite3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_sprite3d_set_scale(sprite3D, x, y, z);

	/** The sprite takes its own reference; a none texture leaves it with nothing to draw. **/
	public static inline function setTexture(sprite3D:Sprite3D, texture:Texture):Bool
		return Raw.wgr_sprite3d_set_texture(sprite3D, texture);

	/** The world size of the quad before scale. A width or height at or below 0 is refused. **/
	public static inline function setExtent(sprite3D:Sprite3D, width:Float, height:Float):Bool
		return Raw.wgr_sprite3d_set_extent(sprite3D, width, height);

	/**
		The region of the texture to show, in texture pixels — for sprite sheets and
		atlases. A width or height at or below 0 goes back to the whole texture.
	**/
	public static inline function setSource(sprite3D:Sprite3D, x:Float, y:Float, width:Float, height:Float):Bool
		return Raw.wgr_sprite3d_set_source(sprite3D, x, y, width, height);

	/**
		The point of the quad that sits on the sprite's position and that it turns
		around, **as a fraction of the quad** rather than in pixels: (0, 0) is its
		top-left, (1, 1) its bottom-right, (0.5, 0.5) its center — the default. y runs
		down the texture, so (0.5, 1) puts the position at the bottom edge, which is
		what a sprite standing on the ground wants.
	**/
	public static overload extern inline function setPivot(sprite3D:Sprite3D, x:Float, y:Float):Bool
		return setPivotRaw(sprite3D, x, y);

	/** The same, as a `Vec2`. **/
	public static overload extern inline function setPivot(sprite3D:Sprite3D, pivot:Vec2):Bool
		return setPivotRaw(sprite3D, pivot.x, pivot.y);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setPivotRaw(sprite3D:Sprite3D, x:Float, y:Float):Bool
		return Raw.wgr_sprite3d_set_pivot(sprite3D, x, y);

	/** The pivot, as set; (0.5, 0.5) by default. **/
	public static inline function getPivot(sprite3D:Sprite3D, ?result:Vec2):Vec2
		return Vec2.of(Raw.wgr_sprite3d_get_pivot(sprite3D), result);

	/**
		How it uses its texture's alpha; `Blend` by default. In a scene, blended sprites
		are sorted back to front with the other transparent parts. Opaque and masked
		sprites — `cutoff` 0..1 cuts out texels below it — write depth and aren't
		sorted, and additive ones are drawn after the blended parts, unsorted. Unsorted
		sprites are grouped by texture, so they draw in fewer batches.
	**/
	public static inline function setAlphaMode(sprite3D:Sprite3D, mode:AlphaMode, cutoff:Float = 0):Bool
		return Raw.wgr_sprite3d_set_alpha_mode(sprite3D, mode, cutoff);

	/**
		Draw it with a material instead of wgrender's sprite shader (texture x tint,
		unlit); a none material goes back to that. It keeps its texture, region, tint,
		facing and alpha mode, and holds its own reference to the material.

		A PBR material lights it like a model, from the scene's lights and environment:
		the sprite's texture is the base color, and the quad's facing is the surface
		normal, so normal maps work on billboards. Sprites in one batch share the lights
		chosen for where that batch is, so one far from the rest can miss a light near it.
	**/
	public static inline function setMaterial(sprite3D:Sprite3D, material:Material):Bool
		return Raw.wgr_sprite3d_set_material(sprite3D, material);

	/** What it draws with, borrowed; none when it's on the built-in sprite shader. **/
	public static inline function getMaterial(sprite3D:Sprite3D):Material
		return (Raw.wgr_sprite3d_get_material(sprite3D) : Handle);

	/**
		Make picking ignore texels whose alpha is below `threshold` (0..1), so the
		transparent corners of a billboard don't catch the pointer. Builds a CPU alpha
		mask from the texture's source path the first time it's needed.
	**/
	public static inline function setPickAlphaTest(sprite3D:Sprite3D, enable:Bool, threshold:Float = 0.5):Bool
		return Raw.wgr_sprite3d_set_pick_alpha_test(sprite3D, enable, threshold);

	/** Draw it once, now, outside any scene. Between `Render.beginMode3D` and its end. **/
	public static inline function draw(sprite3D:Sprite3D):Void
		Raw.wgr_sprite3d_draw(sprite3D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(sprite3D:Sprite3D):Void
		Raw.wgr_sprite3d_destroy(sprite3D);
}
