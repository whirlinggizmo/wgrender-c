package wgr;

// wgr_types.h — how a particle's alpha reaches the framebuffer

enum abstract AlphaMode(Int) to Int {
	/** Alpha ignored. **/
	var Opaque = 0;

	/** Fully opaque or fully transparent, split at a cutoff; depth written. **/
	var Mask = 1;

	/** Alpha blended, back to front. **/
	var Blend = 2;

	/** Added to what's behind — glows, sparks. Not sorted, no depth write. The default. **/
	var Add = 3;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int` — a setter should not take any
		number — so reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):AlphaMode
		return cast v;

	#if cpp
	/** C++ needs the cast: the header says `wgr_alpha_mode_t`, not `int`. **/
	@:to extern inline function toRaw():CAlphaMode
		return untyped __cpp__("(wgr_alpha_mode_t)({0})", this);
	#end
}
