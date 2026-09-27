package wgr;

// wgr_asset.h

/**
	A pending "make this file local" task, or a group of them (`Asset.createGroup`).

	On hxcpp, attach callbacks with `then`: wgrender fires exactly one of them on the
	main thread during a later frame, then frees the task. On js there is no `then` —
	the guest ABI hands the guest an id rather than a task to hang closures on — so
	watch `Asset.getProgress` instead.
**/
@:using(wgr.AssetTask)
abstract AssetTask(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(task:AssetTask):Bool
		return (task : Handle).isNone;

	/**
		Attach callbacks. False, and no callback, if the task is invalid or the queue is
		full. hxcpp only: it needs a C function pointer, which the guest ABI replaces.
	**/
	#if cpp
	public static inline function then(task:AssetTask, onSuccess:(path:String) -> Void,
			?onFailure:(path:String) -> Void):Bool
		return Asset.addTask(cast task, onSuccess, onFailure);
	#end
}
