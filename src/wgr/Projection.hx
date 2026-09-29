package wgr;

// wgr_camera3d.h

enum abstract Projection(Int) to Int {
	var Perspective = 0;
	var Orthographic = 1;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int` — a setter should not take any
		number — so reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):Projection
		return cast v;

	#if cpp
	/** C++ needs the cast: the header says `wgr_camera3d_projection_t`, not `int`. **/
	@:to extern inline function toRaw():CCamera3DProjection
		return untyped __cpp__("(wgr_camera3d_projection_t)({0})", this);
	#end
}
