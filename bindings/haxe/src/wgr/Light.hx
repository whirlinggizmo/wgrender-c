package wgr;

// wgr_light.h

@:using(wgr.Light)
abstract Light(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(light:Light):Bool
		return (light : Handle).isNone;

	/**
		Which kind it was made as. Read only — a light doesn't change kind.
	**/
	public static inline function getType(light:Light):LightKind
		return LightKind.of(Raw.wgr_light_get_type(light));

	/** Full brightness out to here, in radians from the direction. Set with `setSpotCone`. **/
	public static inline function getSpotInnerAngle(light:Light):Float
		return Raw.wgr_light_get_spot_inner_angle(light);

	/** Faded to nothing by here. Set with `setSpotCone`. **/
	public static inline function getSpotOuterAngle(light:Light):Float
		return Raw.wgr_light_get_spot_outer_angle(light);

	/** Always-on part of the depth offset, in shadow-map texels. Set with `setShadowBias`. **/
	public static inline function getShadowBiasConstant(light:Light):Float
		return Raw.wgr_light_get_shadow_bias_constant(light);

	/** The part scaled by how steeply the surface faces the light. Set with `setShadowBias`. **/
	public static inline function getShadowBiasSlope(light:Light):Float
		return Raw.wgr_light_get_shadow_bias_slope(light);

	/** `new Light(...)` is `Light.create(...)`, the same call. **/
	public inline function new(kind:LightKind)
		this = create(kind);

	public static inline function create(kind:LightKind):Light
		return (Raw.wgr_light_create(kind) : Handle);

	/** Directional and spot; default (0, -1, 0). Held normalized, so reading it back
		gives a unit vector rather than what was passed. **/
	public static inline function getDirection(light:Light, ?result:Vec3):Vec3
		return Vec3.of(Raw.wgr_light_get_direction(light), result);

	/** Directional and spot; default (0, -1, 0). Held normalized, so reading it back
		gives a unit vector rather than what was passed. **/
	public static inline function setDirection(light:Light, value:Vec3):Bool
		return Raw.wgr_light_set_direction(light, value.x, value.y, value.z);

	/** Default 1. Below 0 is held as 0. **/
	public static inline function getIntensity(light:Light):Float
		return Raw.wgr_light_get_intensity(light);

	/** Default 1. Below 0 is held as 0. **/
	public static inline function setIntensity(light:Light, value:Float):Bool
		return Raw.wgr_light_set_intensity(light, value);

	/** Default: white. **/
	public static inline function getColor(light:Light):Color
		return Raw.wgr_light_get_color(light);

	/** Default: white. **/
	public static inline function setColor(light:Light, value:Color):Bool
		return Raw.wgr_light_set_color(light, value);

	/** Point and spot. **/
	public static inline function getPosition(light:Light, ?result:Vec3):Vec3
		return Vec3.of(Raw.wgr_light_get_position(light), result);

	/** Point and spot. **/
	public static overload extern inline function setPosition(light:Light, value:Vec3):Bool
		return setPositionRaw(light, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setPosition(light:Light, x:Float, y:Float, z:Float):Bool
		return setPositionRaw(light, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setPositionRaw(light:Light, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_light_set_position(light, x, y, z);

	/** Point and spot; 0 is unlimited, the default. Below 0 is held as 0. **/
	public static inline function getRange(light:Light):Float
		return Raw.wgr_light_get_range(light);

	/** Point and spot; 0 is unlimited, the default. Below 0 is held as 0. **/
	public static inline function setRange(light:Light, value:Float):Bool
		return Raw.wgr_light_set_range(light, value);

	/** Default: enabled. **/
	public static inline function isEnabled(light:Light):Bool
		return Raw.wgr_light_is_enabled(light);

	/** Default: enabled. **/
	public static inline function setEnabled(light:Light, value:Bool):Bool
		return Raw.wgr_light_set_enabled(light, value);

	public static inline function getCastsShadows(light:Light):Bool
		return Raw.wgr_light_get_casts_shadows(light);

	public static inline function setCastsShadows(light:Light, value:Bool):Bool
		return Raw.wgr_light_set_casts_shadows(light, value);

	/**
		How far this light's shadows reach in world units, 50 by default. A directional
		light covers that much of what the camera sees, so less is sharper; a spot
		covers its cone out to this or its range, whichever is nearer. Refused at or
		below 0, which wgrender logs.
	**/
	public static inline function getShadowDistance(light:Light):Float
		return Raw.wgr_light_get_shadow_distance(light);

	/**
		How far this light's shadows reach in world units, 50 by default. A directional
		light covers that much of what the camera sees, so less is sharper; a spot
		covers its cone out to this or its range, whichever is nearer. Refused at or
		below 0, which wgrender logs.
	**/
	public static inline function setShadowDistance(light:Light, value:Float):Bool
		return Raw.wgr_light_set_shadow_distance(light, value);

	/**
		The shadow map's resolution in pixels each way, 2048 by default. Clamped to
		256..4096 and rounded down to a power of two, so 100 reads back 256 and 9000
		reads back 4096. A size below 1 is refused instead, since that is not a size.
		Bigger is sharper and slower, and costs twice the memory each step.

		Reading it gives the power of two the GPU actually gets, not what was asked for.

		The casting lights in a scene share one map and all get the largest size any of
		them asked for, so keep them the same unless you mean it.
	**/
	public static inline function getShadowMapSize(light:Light):Int
		return Raw.wgr_light_get_shadow_map_size(light);

	/**
		The shadow map's resolution in pixels each way, 2048 by default. Clamped to
		256..4096 and rounded down to a power of two, so 100 reads back 256 and 9000
		reads back 4096. A size below 1 is refused instead, since that is not a size.
		Bigger is sharper and slower, and costs twice the memory each step.

		Reading it gives the power of two the GPU actually gets, not what was asked for.

		The casting lights in a scene share one map and all get the largest size any of
		them asked for, so keep them the same unless you mean it.
	**/
	public static inline function setShadowMapSize(light:Light, value:Int):Bool
		return Raw.wgr_light_set_shadow_map_size(light, value);

	/** How much of this light a shadow blocks: 0 none, 1 all of it (the default).
		Clamped to that range, so 1.5 reads back as 1. **/
	public static inline function getShadowStrength(light:Light):Float
		return Raw.wgr_light_get_shadow_strength(light);

	/** How much of this light a shadow blocks: 0 none, 1 all of it (the default).
		Clamped to that range, so 1.5 reads back as 1. **/
	public static inline function setShadowStrength(light:Light, value:Float):Bool
		return Raw.wgr_light_set_shadow_strength(light, value);

	/**
		A colour mixed into what a shadow leaves behind; black by default, meaning
		nothing added. Scaled by how deep the shadow is, so a half-shadowed edge gets
		half of it. The stylised knob, for a blue or warm shadow without changing how
		the rest of the scene is lit.
	**/
	public static inline function getShadowColor(light:Light):Color
		return Raw.wgr_light_get_shadow_color(light);

	/**
		A colour mixed into what a shadow leaves behind; black by default, meaning
		nothing added. Scaled by how deep the shadow is, so a half-shadowed edge gets
		half of it. The stylised knob, for a blue or warm shadow without changing how
		the rest of the scene is lit.
	**/
	public static inline function setShadowColor(light:Light, value:Color):Bool
		return Raw.wgr_light_set_shadow_color(light, value);

	/**
		Full brightness inside `inner`, fading to nothing at `outer`, in radians from the
		direction. Both are clamped to 0..pi/2; defaults pi/6 and pi/4. Read them back
		with `spotInnerAngle` and `spotOuterAngle`.
	**/
	public static inline function setSpotCone(light:Light, inner:Float, outer:Float):Bool
		return Raw.wgr_light_set_spot_cone(light, inner, outer);

	/**
		Depth offsets that keep a surface from shadowing itself, in shadow-map texels —
		what the artifact is made of, so the same numbers hold at any map size or
		distance. Defaults (1, 4). Too little gives lit surfaces a striped "shadow
		acne"; too much and the shadow creeps away from what casts it.
	**/
	public static inline function setShadowBias(light:Light, constant:Float, slope:Float):Bool
		return Raw.wgr_light_set_shadow_bias(light, constant, slope);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(light:Light):Void
		Raw.wgr_light_destroy(light);
}
