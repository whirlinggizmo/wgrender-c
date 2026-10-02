package wgr;

// wgr_asset.h

/** Asset flags, or-ed together. **/
@:using(wgr.AssetFlag)
enum abstract AssetFlag(Int) to Int {
	/**
		Re-download even if cached. Does nothing where nothing can download — a desktop
		build with no fetcher (`Asset.setFetcher`).
	**/
	var ForceFetch = 1 << 0;

	@:op(A | B)
	public static inline function or(a:AssetFlag, b:AssetFlag):AssetFlag
		return cast((a : Int) | (b : Int));
}
