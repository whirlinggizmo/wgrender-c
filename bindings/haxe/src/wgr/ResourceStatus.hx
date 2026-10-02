package wgr;

// wgr_resource.h

/**
	Where a resource loaded from a file stands. A resource loads on create: its handle
	comes back at once, `Pending`, and turns `Ready` or `Failed` in a later frame, at
	the start of it, so a frame that checks sees each change once, in order. One made
	from numbers (a render target, a generated mesh) is `Ready` from the start.
**/
enum abstract ResourceStatus(Int) to Int {
	/** Not a resource of this kind. **/
	var None = 0;

	/** Its file is being made local, prepared or finished. **/
	var Pending = 1;

	var Ready = 2;

	/** The path, the fetch, the file or the decode failed (the log says why). **/
	var Failed = 3;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int`, so reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):ResourceStatus
		return cast v;
}
