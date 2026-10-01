package wgr;

// wgr_pick.h

/**
	Work done by picks since the last reset, for debugging. Fields in the C struct's
	order; see `Touch`.
**/
@:structInit
class PickStats {
	/** Bounding-box tests. **/
	public final broadphaseTests:Int;

	public final broadphaseRejects:Int;

	/** Exact tests against triangles, quads or rectangles. **/
	public final narrowphaseTests:Int;

	public final narrowphaseHits:Int;

	public function new(broadphaseTests, broadphaseRejects, narrowphaseTests, narrowphaseHits) {
		this.broadphaseTests = broadphaseTests;
		this.broadphaseRejects = broadphaseRejects;
		this.narrowphaseTests = narrowphaseTests;
		this.narrowphaseHits = narrowphaseHits;
	}
}
