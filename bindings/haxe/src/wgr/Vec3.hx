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
/**
	A 3D vector, mutable. A getter makes a new one, or fills one you pass as its
	optional last argument and returns it, so a loop can read into one vector it keeps
	and make no garbage (`Model.getPosition(model, pos)`). Mutable, so there are no shared
	constants to change by accident: `zero()` and `one()` make one, and cost nothing when
	passed straight to a setter, which the compiler reduces to plain numbers.
**/
class Vec3 {
	public var x:Float;
	public var y:Float;
	public var z:Float;

	/** No rotation, no offset: what to pass for a part of a transform that is not turned or moved. **/
	public static inline function zero():Vec3
		return new Vec3(0, 0, 0);

	/** Unit scale. **/
	public static inline function one():Vec3
		return new Vec3(1, 1, 1);

	public inline function new(x:Float = 0, y:Float = 0, z:Float = 0) {
		this.x = x;
		this.y = y;
		this.z = z;
	}

	/**
		A getter's result: a new `Vec3`, or `result` filled and returned. hxcpp gives the C
		struct itself; on js, Raw's one reused array (`Raw.vector`), read out at once.
	**/
	@:allow(wgr)
	static extern inline function of(v:#if cpp CVec3 #else Array<Float> #end, ?result:Vec3):Vec3 {
		#if cpp
		if (result == null)
			return new Vec3(v.x, v.y, v.z);
		result.x = v.x;
		result.y = v.y;
		result.z = v.z;
		#else
		if (result == null)
			return new Vec3(v[0], v[1], v[2]);
		result.x = v[0];
		result.y = v[1];
		result.z = v[2];
		#end
		return result;
	}

	public function toString():String
		return '($x, $y, $z)';
}
