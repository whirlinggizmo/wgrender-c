package wgr;

// wgr_light.h

enum abstract LightKind(Int) to Int {
	var Directional = 0;
	var Point = 1;
	var Spot = 2;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int` — a setter should not take any
		number — so reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):LightKind
		return cast v;

	#if cpp
	/** C++ needs the cast: the header says `wgr_light_type_t`, not `int`. **/
	@:to extern inline function toRaw():CLightType
		return untyped __cpp__("(wgr_light_type_t)({0})", this);
	#end
}
