package wgr;

// wgr_model.h — a mesh placed in the world

/**
	A placed instance of a `Mesh`, with its own transform, tint and animation.

	Every operation is a static taking the handle, named after the C call it makes:
	`wgr_model_set_tint` is `Model.setTint`, `wgr_model_is_visible` is
	`Model.isVisible`. Each C call has exactly one member, which is what lets a
	binding be audited mechanically and what keeps four bindings in step. The type
	is `@:using` itself, so each is also a method: `model.setTint(c)` is
	`Model.setTint(model, c)`, and `new Model(mesh)` is `Model.create(mesh)`.

	```haxe
	final model = new Model(mesh);
	model.setPosition(0, 0, 0);
	model.setTint(Color.WHITE);
	scene.add(model);
	```
**/
@:using(wgr.Model)
abstract Model(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(model:Model):Bool
		return (model : Handle).isNone;

	/** `new Model(...)` is `Model.create(...)`, the same call. **/
	public inline function new(mesh:Mesh)
		this = create(mesh);

	/**
		The model takes its own reference to `mesh`, which may be none: an empty model
		can be placed and animated now and given its mesh later with `setMesh`, and
		drawing and animating do nothing until then.
	**/
	public static inline function create(mesh:Mesh):Model
		return (Raw.wgr_model_create(mesh) : Handle);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(model:Model):Void
		Raw.wgr_model_destroy(model);

	/** Swap the geometry, keeping this model's transform, tint and materials. **/
	public static inline function setMesh(model:Model, mesh:Mesh):Bool
		return Raw.wgr_model_set_mesh(model, mesh);

	/**
		The mesh it draws (the model holds the reference), or none. A model is ready
		when its mesh is: `model.getMesh().getStatus() == Ready`.
	**/
	public static inline function getMesh(model:Model):Mesh
		return (Raw.wgr_model_get_mesh(model) : Handle);

	/** Position, rotation (radians) and scale in one call: the cheapest way to move it every frame. **/
	public static inline function setTransform(model:Model, position:Vec3, rotation:Vec3, scale:Vec3):Bool
		return Raw.wgr_model_set_transform(model, position.x, position.y, position.z, rotation.x, rotation.y, rotation.z, scale.x,
			scale.y, scale.z);

	/** One part of the transform, leaving the others as they are. **/
	public static overload extern inline function setPosition(model:Model, value:Vec3):Bool
		return setPositionRaw(model, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setPosition(model:Model, x:Float, y:Float, z:Float):Bool
		return setPositionRaw(model, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setPositionRaw(model:Model, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_model_set_position(model, x, y, z);

	/** Radians. **/
	public static overload extern inline function setRotation(model:Model, value:Vec3):Bool
		return setRotationRaw(model, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setRotation(model:Model, x:Float, y:Float, z:Float):Bool
		return setRotationRaw(model, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setRotationRaw(model:Model, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_model_set_rotation(model, x, y, z);

	public static overload extern inline function setScale(model:Model, value:Vec3):Bool
		return setScaleRaw(model, value.x, value.y, value.z);

	/** The same, without a `Vec3`. **/
	public static overload extern inline function setScale(model:Model, x:Float, y:Float, z:Float):Bool
		return setScaleRaw(model, x, y, z);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setScaleRaw(model:Model, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_model_set_scale(model, x, y, z);

	/** Where it is, as last set. **/
	public static inline function getPosition(model:Model):Vec3
		return Vec3.of(Raw.wgr_model_get_position(model));

	/** Radians, as last set. **/
	public static inline function getRotation(model:Model):Vec3
		return Vec3.of(Raw.wgr_model_get_rotation(model));

	public static inline function getScale(model:Model):Vec3
		return Vec3.of(Raw.wgr_model_get_scale(model));

	public static inline function setTint(model:Model, color:Color):Bool
		return Raw.wgr_model_set_tint(model, color);

	/** Drawn at all. **/
	public static inline function isVisible(model:Model):Bool
		return Raw.wgr_model_is_visible(model);

	public static inline function setVisible(model:Model, value:Bool):Bool
		return Raw.wgr_model_set_visible(model, value);

	/** Whether a pick can hit it. Default: it can. **/
	public static inline function isPickable(model:Model):Bool
		return Raw.wgr_model_is_pickable(model);

	public static inline function setPickable(model:Model, value:Bool):Bool
		return Raw.wgr_model_set_pickable(model, value);

	/**
		Enabled (the default): a hit reacts — hover, press and click in an interactive
		scene. Disabled: still drawn and still picked, and it still blocks the pointer,
		but it doesn't react.
	**/
	public static inline function isEnabled(model:Model):Bool
		return Raw.wgr_model_is_enabled(model);

	public static inline function setEnabled(model:Model, value:Bool):Bool
		return Raw.wgr_model_set_enabled(model, value);

	/**
		Drawn into a casting light's depth map, so it shadows what's behind it. On by
		default; turn it off for a skybox or a glow.
	**/
	public static inline function castsShadow(model:Model):Bool
		return Raw.wgr_model_casts_shadow(model);

	public static inline function setCastsShadow(model:Model, value:Bool):Bool
		return Raw.wgr_model_set_casts_shadow(model, value);

	/** Shadows darken it. On by default; turn it off for something that shouldn't shade. **/
	public static inline function receivesShadow(model:Model):Bool
		return Raw.wgr_model_receives_shadow(model);

	public static inline function setReceivesShadow(model:Model, value:Bool):Bool
		return Raw.wgr_model_set_receives_shadow(model, value);

	// --- animation ---

	public static inline function setAnimation(model:Model, index:Int):Bool
		return Raw.wgr_model_set_animation(model, index);

	/** How many animations the mesh brought. **/
	public static inline function getAnimationCount(model:Model):Int
		return Raw.wgr_model_get_animation_count(model);

	/**
		Where the active animation is posed, in seconds — wrapped when looping, else
		clamped. glTF animations are timed in seconds, not frames. Set before the mesh
		arrives and it applies once it does.
	**/
	public static inline function getAnimationTime(model:Model):Float
		return Raw.wgr_model_get_animation_time(model);

	public static inline function setAnimationTime(model:Model, seconds:Float):Bool
		return Raw.wgr_model_set_animation_time(model, seconds);

	public static inline function setAnimationSpeed(model:Model, speed:Float):Bool
		return Raw.wgr_model_set_animation_speed(model, speed);

	public static inline function setAnimationLoop(model:Model, loop:Bool):Bool
		return Raw.wgr_model_set_animation_loop(model, loop);

	/** How long an animation runs, in seconds; 0 for no such animation or no mesh yet. **/
	public static inline function getAnimationDuration(model:Model, animation:Int):Float
		return Raw.wgr_model_get_animation_duration(model, animation);

	/** Advance the current animation by `dt` seconds. **/
	public static inline function animate(model:Model, dt:Float):Bool
		return Raw.wgr_model_animate(model, dt);

	// --- materials and drawing ---

	/**
		Draw one of the mesh's slots (0..31) with `material` instead of the mesh's own.
		A slot of -1 sets every one, and a none material restores the mesh's. Overrides
		stay when the mesh changes. The model takes its own reference.
	**/
	public static inline function setMaterial(model:Model, slot:Int, material:Material):Bool
		return Raw.wgr_model_set_material(model, slot, material);

	/** What it draws `slot` with, borrowed — the override if there is one, else the mesh's. **/
	public static inline function getMaterial(model:Model, slot:Int):Material
		return (Raw.wgr_model_get_material(model, slot) : Handle);

	/** Draw it once, now, outside any scene. Between `Render.beginMode3D` and its end. **/
	public static inline function draw(model:Model):Void
		Raw.wgr_model_draw(model);
}
