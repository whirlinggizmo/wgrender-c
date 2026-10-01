package wgr;

// wgr_scene.h

/**
	How a scene's lit colors map to the display. Lighting can exceed what a screen
	shows; tone mapping rolls off the highlights instead of clipping them. Applies to
	models and the background, not to sprites, shapes or text.
**/
enum abstract Tonemap(Int) to Int {
	/** Clip. **/
	var None = 0;

	/** Khronos PBR Neutral: colors kept until the highlights roll off. The default. **/
	var Neutral = 1;

	/** Filmic: more contrast, highlights shift toward white. **/
	var Aces = 2;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int` — a setter should not take any
		number — so reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):Tonemap
		return cast v;

	#if cpp
	/** C++ needs the cast: the header says `wgr_tonemap_t`, not `int`. **/
	@:to extern inline function toRaw():CTonemap
		return untyped __cpp__("(wgr_tonemap_t)({0})", this);
	#end
}
