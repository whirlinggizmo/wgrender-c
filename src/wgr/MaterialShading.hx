package wgr;

// wgr_material.h

// `from Int` because wgrender returns it: an enum comes back from C as a number, the
// same way ButtonState does from wgr_input_get_key.
enum abstract MaterialShading(Int) from Int to Int {
	/** glTF metallic-roughness, lit by the scene's lights. **/
	var Pbr = 0;

	/** Base colour x texture x tint; ignores lights (KHR_materials_unlit). **/
	var Unlit = 1;

	/** A custom shader — `Material.custom`, not `new Material(...)`. **/
	var Custom = 2;

	#if cpp
	/** C++ needs the cast: the header says `wgr_material_shading_t`, not `int`. **/
	@:to extern inline function toRaw():CMaterialShading
		return untyped __cpp__("(wgr_material_shading_t)({0})", this);
	#end
}
