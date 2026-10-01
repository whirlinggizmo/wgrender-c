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
class Vec3 {
	public final x:Float;
	public final y:Float;
	public final z:Float;

	/** No rotation, no offset: what to pass for a part of a transform that is not turned or moved. **/
	public static final ZERO:Vec3 = new Vec3(0, 0, 0);

	/** Unit scale. **/
	public static final ONE:Vec3 = new Vec3(1, 1, 1);

	public inline function new(x:Float = 0, y:Float = 0, z:Float = 0) {
		this.x = x;
		this.y = y;
		this.z = z;
	}

	/**
		The C struct as a `Vec3`. hxcpp gives us the struct itself; on js Raw has already
		read it out of the heap, so there is nothing left to do.
	**/
	@:allow(wgr)
	static extern inline function of(v:#if cpp CVec3 #else Vec3 #end):Vec3
		#if cpp
		return new Vec3(v.x, v.y, v.z);
		#else
		return v;
		#end

	public function toString():String
		return '($x, $y, $z)';
}
