package wgr;

// wgr_material.h — how a surface is shaded

/**
	A material: reference counted and shared, like a `Texture` or a `Mesh`. Assign it
	to a model's slot with `Model.setMaterial`; the model takes its own reference, so
	release yours when done.

	A new one carries glTF's defaults — white, fully metallic, fully rough, opaque,
	single sided — and shading follows glTF metallic-roughness in linear colour space.

	Parameters are set by name. The built-ins have properties here, which is the
	difference between a typo being a compile error and a silent `false`:

	```haxe
	final m = new Material(Pbr);
	m.setRoughness(0.35);
	m.setBaseColorTexture(albedo);
	m.setBaseColor(0.9, 0.2, 0.2);   // linear rgba, glTF's factors
	model.setMaterial(0, m);
	m.release();                     // the model holds its own reference
	```

	A custom shader's parameters are whatever it declares, so those go through the
	`set*` methods by name.
**/
@:using(wgr.Material)
abstract Material(Handle) from Handle to Handle {
	// --- the built-in glTF parameters, by name in the header's table ---

	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(material:Material):Bool
		return (material : Handle).isNone;

	/** `new Material(...)` is `Material.create(...)`, the same call. **/
	public inline function new(shading:MaterialShading = Pbr)
		this = create(shading);

	public static inline function create(shading:MaterialShading = Pbr):Material
		return (Raw.wgr_material_create(shading) : Handle);

	/**
		A material drawn by a custom shader; it holds its own reference to the shader.
		The shader may still be loading: settings made meanwhile are kept by name and
		applied once it's `Ready` (an unknown name is logged then), and until then what
		uses the material isn't drawn. If the shader `Failed`, a model draws flat
		magenta, a sprite plainly, and an effect not at all.
	**/
	public static inline function custom(shader:Handle):Material
		return (Raw.wgr_material_create_custom(shader) : Handle);

	/** Its custom shader, or none for built-in shading. **/
	public static inline function getShader(material:Material):Handle
		return Raw.wgr_material_get_shader(material);

	/** Drop this reference; the material goes when the last one does. **/
	public static inline function release(material:Material):Void
		Raw.wgr_material_release(material);

	/** Built-in shading mode; `Custom` only ever comes from `Material.custom`. **/
	public static inline function getShading(material:Material):MaterialShading
		return Raw.wgr_material_get_shading(material);

	/** Built-in shading mode; `Custom` only ever comes from `Material.custom`. **/
	public static inline function setShading(material:Material, value:MaterialShading):Bool
		return Raw.wgr_material_set_shading(material, value);

	/** Drawn from both sides. Default: single sided. **/
	public static inline function isDoubleSided(material:Material):Bool
		return Raw.wgr_material_is_double_sided(material);

	/** Drawn from both sides. Default: single sided. **/
	public static inline function setDoubleSided(material:Material, value:Bool):Bool
		return Raw.wgr_material_set_double_sided(material, value);

	/** How it uses alpha. Set it with `setAlphaMode`. **/
	public static inline function getAlphaMode(material:Material):AlphaMode
		return AlphaMode.of(Raw.wgr_material_get_alpha_mode(material));

	/**
		How it uses alpha; `cutoff` applies to `Mask`. `Add` is not supported for
		materials yet and is refused — additive belongs to sprites and particles.
	**/
	public static inline function setAlphaMode(material:Material, mode:AlphaMode, cutoff:Float = 0.5):Bool
		return Raw.wgr_material_set_alpha_mode(material, mode, cutoff);

	/** 0..1. **/
	/** 0..1. **/
	public static inline function setMetallic(material:Material, value:Float):Bool
		return setFloat(material, "metallic", value);

	/** 0..1. **/
	/** 0..1. **/
	public static inline function setRoughness(material:Material, value:Float):Bool
		return setFloat(material, "roughness", value);

	public static inline function setNormalScale(material:Material, value:Float):Bool
		return setFloat(material, "normal_scale", value);

	/** 0..1. **/
	/** 0..1. **/
	public static inline function setOcclusionStrength(material:Material, value:Float):Bool
		return setFloat(material, "occlusion_strength", value);

	/** sRGB rgba. **/
	/** sRGB rgba. **/
	public static inline function setBaseColorTexture(material:Material, value:Texture):Bool
		return setTexture(material, "base_color_texture", value);

	/** Green is roughness, blue is metallic. **/
	/** Green is roughness, blue is metallic. **/
	public static inline function setMetallicRoughnessTexture(material:Material, value:Texture):Bool
		return setTexture(material, "metallic_roughness_texture", value);

	/** Tangent-space normal map. **/
	/** Tangent-space normal map. **/
	public static inline function setNormalTexture(material:Material, value:Texture):Bool
		return setTexture(material, "normal_texture", value);

	/** Red is ambient occlusion. **/
	/** Red is ambient occlusion. **/
	public static inline function setOcclusionTexture(material:Material, value:Texture):Bool
		return setTexture(material, "occlusion_texture", value);

	/** sRGB rgb. **/
	/** sRGB rgb. **/
	public static inline function setEmissiveTexture(material:Material, value:Texture):Bool
		return setTexture(material, "emissive_texture", value);

	/** Linear rgba, glTF's factor — not an sRGB `Color`. Alpha drives Mask and Blend. **/
	public static inline function setBaseColor(material:Material, r:Float, g:Float, b:Float, a:Float = 1):Bool
		return setVec4(material, "base_color", r, g, b, a);

	/** Linear rgb, and may exceed 1. **/
	public static inline function setEmissive(material:Material, r:Float, g:Float, b:Float):Bool
		return setVec3(material, "emissive", r, g, b);

	// --- by name, for a custom shader's own parameters ---

	public static inline function setInt(material:Material, name:String, value:Int):Bool
		return Raw.wgr_material_set_int(material, name, value);

	public static inline function setFloat(material:Material, name:String, value:Float):Bool
		return Raw.wgr_material_set_float(material, name, value);

	public static inline function setVec2(material:Material, name:String, x:Float, y:Float):Bool
		return Raw.wgr_material_set_vec2(material, name, x, y);

	public static inline function setVec3(material:Material, name:String, x:Float, y:Float, z:Float):Bool
		return Raw.wgr_material_set_vec3(material, name, x, y, z);

	public static inline function setVec4(material:Material, name:String, x:Float, y:Float, z:Float, w:Float):Bool
		return Raw.wgr_material_set_vec4(material, name, x, y, z, w);

	/** An sRGB colour handle, converted to linear. **/
	public static inline function setColor(material:Material, name:String, color:Color):Bool
		return Raw.wgr_material_set_color(material, name, color);

	public static inline function setTexture(material:Material, name:String, texture:Texture):Bool
		return Raw.wgr_material_set_texture(material, name, texture);

	public static inline function setTextureSampling(material:Material, name:String, wrapU:TextureWrap, wrapV:TextureWrap,
			filter:TextureFilter):Bool
		return Raw.wgr_material_set_texture_sampling(material, name, wrapU, wrapV, filter);
}
