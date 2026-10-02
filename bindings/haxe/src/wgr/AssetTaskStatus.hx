package wgr;

// wgr_asset.h

/**
	Where an asset task stands (`Asset.ensure`, `Asset.createGroup`). It comes back
	`Pending` and turns `Done` or `Failed` in a later frame, at the start of it, so a
	frame that checks sees each change once, in order.
**/
enum abstract AssetTaskStatus(Int) to Int {
	/** Not a task, or one destroyed. **/
	var None = 0;

	var Pending = 1;

	/** The file is local, and every file it names; for a group, every member is. **/
	var Done = 2;

	/** The path, the fetch or a file it names failed (the log says why); for a group, a member did. **/
	var Failed = 3;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int`, so reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):AssetTaskStatus
		return cast v;
}
