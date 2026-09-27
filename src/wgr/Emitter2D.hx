package wgr;

// wgr_emitter2d.h — particles on the screen

/**
	The same as `Emitter3D`, on the screen instead of in the world: positions and
	velocities are in logical pixels, and it draws over the 3D like `Sprite2D`.
**/
@:using(wgr.Emitter2D)
abstract Emitter2D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(emitter2D:Emitter2D):Bool
		return (emitter2D : Handle).isNone;

	/** Where it is, as last set. **/
	public static inline function getPosition(emitter2D:Emitter2D):Vec2
		return Vec2.of(Raw.wgr_emitter2d_get_position(emitter2D));

	/** `new Emitter2D(...)` is `Emitter2D.create(...)`, the same call. **/
	public inline function new(texture:Texture)
		this = create(texture);

	public static inline function create(texture:Texture):Emitter2D
		return (Raw.wgr_emitter2d_create(texture) : Handle);

	/** Where new particles appear; the ones alive stay where they were born. **/
	public static overload extern inline function setPosition(emitter2D:Emitter2D, position:Vec2):Bool
		return Raw.wgr_emitter2d_set_position(emitter2D, position.x, position.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function setPosition(emitter2D:Emitter2D, x:Float, y:Float):Bool
		return Raw.wgr_emitter2d_set_position(emitter2D, x, y);

	/** Move without the trail a `setPosition` sweep would leave. **/
	public static overload extern inline function jump(emitter2D:Emitter2D, position:Vec2):Bool
		return Raw.wgr_emitter2d_jump(emitter2D, position.x, position.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function jump(emitter2D:Emitter2D, x:Float, y:Float):Bool
		return Raw.wgr_emitter2d_jump(emitter2D, x, y);

	/** New particles appear anywhere in this box around the position. **/
	public static inline function setSpawnBox(emitter2D:Emitter2D, halfWidth:Float, halfHeight:Float):Bool
		return Raw.wgr_emitter2d_set_spawn_box(emitter2D, halfWidth, halfHeight);

	/** 3D's spawn sphere. **/
	public static inline function setSpawnCircle(emitter2D:Emitter2D, radius:Float):Bool
		return Raw.wgr_emitter2d_set_spawn_circle(emitter2D, radius);

	/** Initial velocity, `spread` radians of arc around it, and its variance. **/
	public static inline function setVelocity(emitter2D:Emitter2D, velocity:Vec2, spread:Float = 0, speedVariance:Float = 0):Bool
		return Raw.wgr_emitter2d_set_velocity(emitter2D, velocity.x, velocity.y, spread, speedVariance);

	public static inline function setGravity(emitter2D:Emitter2D, gravity:Vec2):Bool
		return Raw.wgr_emitter2d_set_gravity(emitter2D, gravity.x, gravity.y);

	/** Whether new particles are made; the ones alive finish their lives either way. **/
	public static inline function isEmitting(emitter2D:Emitter2D):Bool
		return Raw.wgr_emitter2d_is_emitting(emitter2D);

	/** Whether new particles are made; the ones alive finish their lives either way. **/
	public static inline function setEmitting(emitter2D:Emitter2D, value:Bool):Bool
		return Raw.wgr_emitter2d_set_emitting(emitter2D, value);

	public static inline function setVisible(emitter2D:Emitter2D, value:Bool):Bool
		return Raw.wgr_emitter2d_set_visible(emitter2D, value);

	/** Particles alive now. **/
	public static inline function getCount(emitter2D:Emitter2D):Int
		return Raw.wgr_emitter2d_get_count(emitter2D);

	/**
		The most that can be alive at once; 1024 by default. Must be 1..65536 — a value
		outside that is refused rather than clamped, and the emitter keeps the max it
		had, which is why this is a method and not a property. Setting it clears the
		particles that were alive.
	**/
	public static inline function setMax(emitter2D:Emitter2D, count:Int):Bool
		return Raw.wgr_emitter2d_set_max(emitter2D, count);

	/** Made per second while emitting; 0 for bursts only. A negative rate is refused. **/
	public static inline function setRate(emitter2D:Emitter2D, value:Float):Bool
		return Raw.wgr_emitter2d_set_rate(emitter2D, value);

	/** Velocity lost per second — particles that slow down rather than coast. **/
	public static inline function setDrag(emitter2D:Emitter2D, value:Float):Bool
		return Raw.wgr_emitter2d_set_drag(emitter2D, value);

	/** How much of a moving emitter's own velocity a new particle is given. **/
	public static inline function setInheritVelocity(emitter2D:Emitter2D, value:Float):Bool
		return Raw.wgr_emitter2d_set_inherit_velocity(emitter2D, value);

	/** Stretch along the direction of travel, in seconds of motion. **/
	public static inline function setStretch(emitter2D:Emitter2D, value:Float):Bool
		return Raw.wgr_emitter2d_set_stretch(emitter2D, value);

	/** Fixes the random sequence, so a run repeats. **/
	public static inline function setSeed(emitter2D:Emitter2D, value:Int):Bool
		return Raw.wgr_emitter2d_set_seed(emitter2D, value);

	/** How long a particle lives, picked per particle between the two. **/
	public static inline function setLife(emitter2D:Emitter2D, minSeconds:Float, maxSeconds:Float):Bool
		return Raw.wgr_emitter2d_set_life(emitter2D, minSeconds, maxSeconds);

	/** Start and end size, and how much that varies per particle. **/
	public static inline function setSize(emitter2D:Emitter2D, start:Float, end:Float, variance:Float = 0):Bool
		return Raw.wgr_emitter2d_set_size(emitter2D, start, end, variance);

	/** Start and end tint; the end's alpha is usually 0, so particles fade out. **/
	public static inline function setColor(emitter2D:Emitter2D, start:Color, end:Color):Bool
		return Raw.wgr_emitter2d_set_color(emitter2D, start, end);

	/** Turn rate, picked per particle between the two (radians per second). **/
	public static inline function setSpin(emitter2D:Emitter2D, min:Float, max:Float):Bool
		return Raw.wgr_emitter2d_set_spin(emitter2D, min, max);

	/** The sub-rectangle of the texture a particle is cut from. **/
	public static inline function setSource(emitter2D:Emitter2D, x:Float, y:Float, width:Float, height:Float):Bool
		return Raw.wgr_emitter2d_set_source(emitter2D, x, y, width, height);

	/** A flipbook: `columns` by `rows` frames, `count` of them (0: all). **/
	public static inline function setFrames(emitter2D:Emitter2D, columns:Int, rows:Int, count:Int = 0, perSecond:Float = 0):Bool
		return Raw.wgr_emitter2d_set_frames(emitter2D, columns, rows, count, perSecond);

	public static inline function setAlphaMode(emitter2D:Emitter2D, mode:AlphaMode, cutoff:Float = 0):Bool
		return Raw.wgr_emitter2d_set_alpha_mode(emitter2D, mode, cutoff);

	/** Make `count` particles now, on top of whatever the rate is doing. **/
	public static inline function burst(emitter2D:Emitter2D, count:Int):Bool
		return Raw.wgr_emitter2d_burst(emitter2D, count);

	/** Run it forward `seconds` so it is already going when it first appears. **/
	public static inline function prewarm(emitter2D:Emitter2D, seconds:Float):Bool
		return Raw.wgr_emitter2d_prewarm(emitter2D, seconds);

	/** A size curve, in place of the start/end pair: `t` from 0 to 1 over a life. **/
	public static inline function addSizeKey(emitter2D:Emitter2D, t:Float, size:Float):Bool
		return Raw.wgr_emitter2d_add_size_key(emitter2D, t, size);

	public static inline function clearSizeKeys(emitter2D:Emitter2D):Bool
		return Raw.wgr_emitter2d_clear_size_keys(emitter2D);

	/** A colour curve, in place of the start/end pair. **/
	public static inline function addColorKey(emitter2D:Emitter2D, t:Float, color:Color):Bool
		return Raw.wgr_emitter2d_add_color_key(emitter2D, t, color);

	public static inline function clearColorKeys(emitter2D:Emitter2D):Bool
		return Raw.wgr_emitter2d_clear_color_keys(emitter2D);

	/** Each particle takes one of these at birth, instead of the start tint. **/
	public static inline function addPaletteColor(emitter2D:Emitter2D, color:Color):Bool
		return Raw.wgr_emitter2d_add_palette_color(emitter2D, color);

	public static inline function clearPalette(emitter2D:Emitter2D):Bool
		return Raw.wgr_emitter2d_clear_palette(emitter2D);

	/** Every particle alive, gone. **/
	public static inline function clear(emitter2D:Emitter2D):Void
		Raw.wgr_emitter2d_clear(emitter2D);

	/** Draw it now; a scene draws its members itself. **/
	public static inline function draw(emitter2D:Emitter2D):Void
		Raw.wgr_emitter2d_draw(emitter2D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(emitter2D:Emitter2D):Void
		Raw.wgr_emitter2d_destroy(emitter2D);
}
