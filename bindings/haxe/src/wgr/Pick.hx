package wgr;

// wgr_pick.h — hit-testing one object

/**
	Picking a single object. To pick among many, add them to a `Scene` and use
	`Scene.pick`, which returns the nearest hit and tests 2D members first.
**/
class Pick {
	/**
		Whether (`x`, `y`) — a screen point in logical pixels, the mouse say — hits
		`object`. 2D objects are hit-tested in screen space; 3D ones with a ray through
		`camera`, or the active camera when that is none. An object that is hidden or
		not pickable is never hit.
	**/
	public static inline function object(object:SceneMember, x:Float, y:Float, ?camera:Camera3D):PickResult
		return Scene.toPickResult(Raw.wgr_pick_object(object, camera == null ? 0 : (camera : Handle), x, y));

	/** Work done by picks since the last `resetStats`, for debugging. **/
	public static function getStats():PickStats {
		#if cpp
		final s = Raw.wgr_pick_get_stats();
		return new PickStats(s.broadphase_tests, s.broadphase_rejects, s.narrowphase_tests, s.narrowphase_hits);
		#else
		return Raw.wgr_pick_get_stats();
		#end
	}

	public static inline function resetStats():Void
		Raw.wgr_pick_reset_stats();
}
