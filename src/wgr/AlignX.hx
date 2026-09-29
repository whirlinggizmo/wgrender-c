package wgr;

// wgr_text.h

/**
	Where a block of text sits horizontally relative to its position. wgrender has one
	C enum for both axes and returns false for a value meant for the other one; this
	splits it in two, so `setAlign(Top, Left)` doesn't compile.
**/
enum abstract AlignX(Int) to Int {
	var Left = 0;
	var Center = 1;
	var Right = 2;

	#if cpp
	/** C++ needs the cast: the header says `wgr_text_align_t`, not `int`. **/
	@:to extern inline function toRaw():CTextAlign
		return untyped __cpp__("(wgr_text_align_t)({0})", this);
	#end
}
