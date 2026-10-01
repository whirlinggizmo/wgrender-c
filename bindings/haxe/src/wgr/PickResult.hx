package wgr;

// wgr_pick.h — what a pick hit

@:structInit
class PickResult {
	public final hit:Bool;

	/** Compare with typed handles: `pick.handle == model`. **/
	public final handle:Handle;

	/** World-space distance from the ray origin to the hit. **/
	public final distance:Float;

	public final pointLocal:Vec3;
	public final pointWorld:Vec3;
	public final normalLocal:Vec3;
	public final normalWorld:Vec3;

	public function new(hit, handle, distance, pointLocal, pointWorld, normalLocal, normalWorld) {
		this.hit = hit;
		this.handle = handle;
		this.distance = distance;
		this.pointLocal = pointLocal;
		this.pointWorld = pointWorld;
		this.normalLocal = normalLocal;
		this.normalWorld = normalWorld;
	}
}
