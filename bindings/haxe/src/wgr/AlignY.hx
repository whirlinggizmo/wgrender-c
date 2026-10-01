package wgr;

// wgr_text.h

/** Where a block of text sits vertically relative to its position. See `AlignX`. **/
enum abstract AlignY(Int) to Int {
	var Top = 3;
	var Middle = 4;
	var Bottom = 5;

	#if cpp
	/** C++ needs the cast: the header says `wgr_text_align_t`, not `int`. **/
	@:to extern inline function toRaw():CTextAlign
		return untyped __cpp__("(wgr_text_align_t)({0})", this);
	#end
}
