package wgr;

// wgr_texture.h

enum abstract TextureWrap(Int) to Int {
	/** Tile. **/
	var Repeat = 0;

	/** Stretch the edge texels. **/
	var Clamp = 1;

	/** Tile, flipping every other copy. **/
	var Mirror = 2;

	#if cpp
	/** C++ needs the cast: the header says `wgr_texture_wrap_t`, not `int`. **/
	@:to extern inline function toRaw():CTextureWrap
		return untyped __cpp__("(wgr_texture_wrap_t)({0})", this);
	#end
}
