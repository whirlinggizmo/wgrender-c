package wgr;

// wgr_emitter3d.h — particles in the world

/**
	A particle emitter placed in the 3D scene. Add it to a `Scene` and `Scene.draw`
	draws it; `draw` draws it on its own, inside `Render.beginMode3D`.

	It takes its own reference to the texture, so the caller can release theirs.

	Particles are unlit: the texture times the particle's color, with no material
	and no scene lighting. A lit effect wants `Sprite3D` objects with a material.
**/
@:using(wgr.Emitter3D)
abstract Emitter3D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(emitter3D:Emitter3D):Bool
		return (emitter3D : Handle).isNone;

	/** Where it is, as last set. **/
	public static inline function getPosition(emitter3D:Emitter3D):Vec3
		return Vec3.of(Raw.wgr_emitter3d_get_position(emitter3D));

	/** `new Emitter3D(...)` is `Emitter3D.create(...)`, the same call. **/
	public inline function new(texture:Texture)
		this = create(texture);

	public static inline function create(texture:Texture):Emitter3D
		return (Raw.wgr_emitter3d_create(texture) : Handle);

	/** Where new particles appear; the ones alive stay where they were born. **/
	public static overload extern inline function setPosition(emitter3D:Emitter3D, position:Vec3):Bool
		return Raw.wgr_emitter3d_set_position(emitter3D, position.x, position.y, position.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setPosition(emitter3D:Emitter3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_emitter3d_set_position(emitter3D, x, y, z);

	/** Move without the trail a `setPosition` sweep would leave. **/
	public static overload extern inline function jump(emitter3D:Emitter3D, position:Vec3):Bool
		return Raw.wgr_emitter3d_jump(emitter3D, position.x, position.y, position.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function jump(emitter3D:Emitter3D, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_emitter3d_jump(emitter3D, x, y, z);

	/** New particles appear anywhere in this box around the position. **/
	public static inline function setSpawnBox(emitter3D:Emitter3D, half:Vec3):Bool
		return Raw.wgr_emitter3d_set_spawn_box(emitter3D, half.x, half.y, half.z);

	public static inline function setSpawnSphere(emitter3D:Emitter3D, radius:Float):Bool
		return Raw.wgr_emitter3d_set_spawn_sphere(emitter3D, radius);

	/** Initial velocity, `spread` radians of cone around it, and its variance. **/
	public static inline function setVelocity(emitter3D:Emitter3D, velocity:Vec3, spread:Float = 0, speedVariance:Float = 0):Bool
		return Raw.wgr_emitter3d_set_velocity(emitter3D, velocity.x, velocity.y, velocity.z, spread, speedVariance);

	public static inline function setGravity(emitter3D:Emitter3D, gravity:Vec3):Bool
		return Raw.wgr_emitter3d_set_gravity(emitter3D, gravity.x, gravity.y, gravity.z);

	/** Whether new particles are made; the ones alive finish their lives either way. **/
	public static inline function isEmitting(emitter3D:Emitter3D):Bool
		return Raw.wgr_emitter3d_is_emitting(emitter3D);

	/** Whether new particles are made; the ones alive finish their lives either way. **/
	public static inline function setEmitting(emitter3D:Emitter3D, value:Bool):Bool
		return Raw.wgr_emitter3d_set_emitting(emitter3D, value);

	public static inline function setVisible(emitter3D:Emitter3D, value:Bool):Bool
		return Raw.wgr_emitter3d_set_visible(emitter3D, value);

	/** Particles alive now. **/
	public static inline function getCount(emitter3D:Emitter3D):Int
		return Raw.wgr_emitter3d_get_count(emitter3D);

	/**
		The most that can be alive at once; 1024 by default. Must be 1..65536 — a value
		outside that is refused rather than clamped, and the emitter keeps the max it
		had, which is why this is a method and not a property. Setting it clears the
		particles that were alive.
	**/
	public static inline function setMax(emitter3D:Emitter3D, count:Int):Bool
		return Raw.wgr_emitter3d_set_max(emitter3D, count);

	/** Made per second while emitting; 0 for bursts only. A negative rate is refused. **/
	public static inline function setRate(emitter3D:Emitter3D, value:Float):Bool
		return Raw.wgr_emitter3d_set_rate(emitter3D, value);

	/** Velocity lost per second — particles that slow down rather than coast. **/
	public static inline function setDrag(emitter3D:Emitter3D, value:Float):Bool
		return Raw.wgr_emitter3d_set_drag(emitter3D, value);

	/** How much of a moving emitter's own velocity a new particle is given. **/
	public static inline function setInheritVelocity(emitter3D:Emitter3D, value:Float):Bool
		return Raw.wgr_emitter3d_set_inherit_velocity(emitter3D, value);

	/** Stretch along the direction of travel, in seconds of motion. **/
	public static inline function setStretch(emitter3D:Emitter3D, value:Float):Bool
		return Raw.wgr_emitter3d_set_stretch(emitter3D, value);

	/** Fixes the random sequence, so a run repeats. **/
	public static inline function setSeed(emitter3D:Emitter3D, value:Int):Bool
		return Raw.wgr_emitter3d_set_seed(emitter3D, value);

	/** How long a particle lives, picked per particle between the two. **/
	public static inline function setLife(emitter3D:Emitter3D, minSeconds:Float, maxSeconds:Float):Bool
		return Raw.wgr_emitter3d_set_life(emitter3D, minSeconds, maxSeconds);

	/** Start and end size, and how much that varies per particle. **/
	public static inline function setSize(emitter3D:Emitter3D, start:Float, end:Float, variance:Float = 0):Bool
		return Raw.wgr_emitter3d_set_size(emitter3D, start, end, variance);

	/** Start and end tint; the end's alpha is usually 0, so particles fade out. **/
	public static inline function setColor(emitter3D:Emitter3D, start:Color, end:Color):Bool
		return Raw.wgr_emitter3d_set_color(emitter3D, start, end);

	/** Turn rate, picked per particle between the two (radians per second). **/
	public static inline function setSpin(emitter3D:Emitter3D, min:Float, max:Float):Bool
		return Raw.wgr_emitter3d_set_spin(emitter3D, min, max);

	/** The sub-rectangle of the texture a particle is cut from. **/
	public static inline function setSource(emitter3D:Emitter3D, x:Float, y:Float, width:Float, height:Float):Bool
		return Raw.wgr_emitter3d_set_source(emitter3D, x, y, width, height);

	/** A flipbook: `columns` by `rows` frames, `count` of them (0: all). **/
	public static inline function setFrames(emitter3D:Emitter3D, columns:Int, rows:Int, count:Int = 0, perSecond:Float = 0):Bool
		return Raw.wgr_emitter3d_set_frames(emitter3D, columns, rows, count, perSecond);

	public static inline function setAlphaMode(emitter3D:Emitter3D, mode:AlphaMode, cutoff:Float = 0):Bool
		return Raw.wgr_emitter3d_set_alpha_mode(emitter3D, mode, cutoff);

	/** Make `count` particles now, on top of whatever the rate is doing. **/
	public static inline function burst(emitter3D:Emitter3D, count:Int):Bool
		return Raw.wgr_emitter3d_burst(emitter3D, count);

	/** Run it forward `seconds` so it is already going when it first appears. **/
	public static inline function prewarm(emitter3D:Emitter3D, seconds:Float):Bool
		return Raw.wgr_emitter3d_prewarm(emitter3D, seconds);

	/** A size curve, in place of the start/end pair: `t` from 0 to 1 over a life. **/
	public static inline function addSizeKey(emitter3D:Emitter3D, t:Float, size:Float):Bool
		return Raw.wgr_emitter3d_add_size_key(emitter3D, t, size);

	public static inline function clearSizeKeys(emitter3D:Emitter3D):Bool
		return Raw.wgr_emitter3d_clear_size_keys(emitter3D);

	/** A colour curve, in place of the start/end pair. **/
	public static inline function addColorKey(emitter3D:Emitter3D, t:Float, color:Color):Bool
		return Raw.wgr_emitter3d_add_color_key(emitter3D, t, color);

	public static inline function clearColorKeys(emitter3D:Emitter3D):Bool
		return Raw.wgr_emitter3d_clear_color_keys(emitter3D);

	/** Each particle takes one of these at birth, instead of the start tint. **/
	public static inline function addPaletteColor(emitter3D:Emitter3D, color:Color):Bool
		return Raw.wgr_emitter3d_add_palette_color(emitter3D, color);

	public static inline function clearPalette(emitter3D:Emitter3D):Bool
		return Raw.wgr_emitter3d_clear_palette(emitter3D);

	/** Every particle alive, gone. **/
	public static inline function clear(emitter3D:Emitter3D):Void
		Raw.wgr_emitter3d_clear(emitter3D);

	/** Draw it now; a scene draws its members itself. **/
	public static inline function draw(emitter3D:Emitter3D):Void
		Raw.wgr_emitter3d_draw(emitter3D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(emitter3D:Emitter3D):Void
		Raw.wgr_emitter3d_destroy(emitter3D);
}
