package wgr;

// wgr_asset.h

/**
	A "make this file local" task (`Asset.ensure`), a group of them
	(`Asset.createGroup`), or a ping (`Asset.pingHost`). Nothing is called back: read its status in a frame, and
	destroy it when done with it. It is kept until then, so its status and path can be
	read any number of times. Written as methods too (`task.getStatus()`), through
	`@:using`.
**/
@:using(wgr.AssetTask)
abstract AssetTask(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(task:AssetTask):Bool
		return (task : Handle).isNone;

	/** `Pending`, `Done` or `Failed`; `None` for anything that isn't a task. **/
	public static inline function getStatus(task:AssetTask):AssetTaskStatus
		return AssetTaskStatus.of(Raw.wgr_asset_task_get_status(task));

	/**
		The local path of a `Done` file task, directly openable: where the file was
		found (a redirect's, a `fetchUrl`'s). `""` until then, for a group, and for
		anything that isn't a task.
	**/
	public static inline function getPath(task:AssetTask):String
		return Raw.wgr_asset_task_get_path(task);

	/**
		Roughly how far it has got, 0 to 1: a file counts half for being made local and
		half for the files it names; a group, its members' average. 1 once it is `Done`
		or `Failed`, 0 for anything that isn't a task. For resources loading, read their
		statuses (`texture.getStatus()`).
	**/
	public static inline function getProgress(task:AssetTask):Float
		return Raw.wgr_asset_task_get_progress(task);

	/**
		Free it. One still `Pending` runs on and its result is dropped (a file still
		lands in the cache). Destroying a group destroys its members. False for anything
		that isn't a task.
	**/
	public static inline function destroy(task:AssetTask):Bool
		return Raw.wgr_asset_task_destroy(task);

	/**
		The round trip of a `Done` ping, in milliseconds (0 on desktop); 0 for one that
		isn't `Done`, and for anything that isn't a ping.
	**/
	public static inline function getPingMilliseconds(ping:AssetTask):Float
		return Raw.wgr_asset_ping_get_milliseconds(ping);
}
