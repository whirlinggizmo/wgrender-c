package wgr;

// wgr_types.h

// `of` is inline and takes the C struct, so hxcpp writes it into this class's
// generated header -- which is not where @:include on the extern puts wgr.h. On
// Linux the translation unit happened to have it already; MSVC said
// "syntax error: identifier 'vec3_t'", which is the same bug either way.
//
// @:headerCode, not @:headerInclude: the latter is not hxcpp metadata, and Haxe
// ignores an unknown @: without a word, so it looked applied and did nothing.
#if cpp
@:headerCode('#include <wgr.h>')
#end
@:structInit
class Vec2 {
	public final x:Float;
	public final y:Float;

	/** No rotation, no offset: what to pass for a part of a transform that is not turned or moved. **/
	public static final ZERO:Vec2 = new Vec2(0, 0);

	/** Unit scale. **/
	public static final ONE:Vec2 = new Vec2(1, 1);

	public inline function new(x:Float = 0, y:Float = 0) {
		this.x = x;
		this.y = y;
	}

	/**
		The C struct as a `Vec2`. hxcpp gives us the struct itself; on js Raw has already
		read it out of the heap, so there is nothing left to do.
	**/
	@:allow(wgr)
	static extern inline function of(v:#if cpp CVec2 #else Vec2 #end):Vec2
		#if cpp
		return new Vec2(v.x, v.y);
		#else
		return v;
		#end

	public function toString():String
		return '($x, $y)';
}
