package wgr;

// wgr_sprite3d.h

enum abstract SpriteFacing(Int) to Int {
	/** Parallel to the view plane. **/
	var Camera = 0;

	/** Turns about world Y to face the camera, stays upright. **/
	var CameraFixedY = 1;

	/** Flat in the XZ plane, normal +Y. **/
	var YUp = 2;

	/** Its own rotation: the local XY plane, facing +Z. **/
	var Free = 3;

	#if cpp
	/** C++ needs the cast: the header says `wgr_sprite3d_facing_t`, not `int`. **/
	@:to extern inline function toRaw():CSprite3DFacing
		return untyped __cpp__("(wgr_sprite3d_facing_t)({0})", this);
	#end
}
