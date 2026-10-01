package wgr;

// wgr_texture.h

enum abstract TextureFilter(Int) to Int {
	/** Smooth; blends mipmap levels when minified. **/
	var Linear = 0;

	/** Sharp texels — pixel art. **/
	var Nearest = 1;

	#if cpp
	/** C++ needs the cast: the header says `wgr_texture_filter_t`, not `int`. **/
	@:to extern inline function toRaw():CTextureFilter
		return untyped __cpp__("(wgr_texture_filter_t)({0})", this);
	#end
}
