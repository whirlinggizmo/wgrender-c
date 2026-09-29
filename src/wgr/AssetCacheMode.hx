package wgr;

// wgr_asset.h

/**
	How a cached asset is treated on a later visit (`Asset.setCacheMode`). On the web
	the cache keeps each file with what its response said about it; on desktop,
	downloads in the cache directory are used as they are in every mode.
**/
enum abstract AssetCacheMode(Int) to Int {
	/**
		The default: a copy still fresh by its Cache-Control is used without a request;
		any other is checked with the host first. 304 keeps it, 200 replaces it, 4xx
		deletes it and fails the load, and no answer (offline) or a 5xx uses it.
	**/
	var Revalidate = 0;

	/** A cached copy is used without asking, however old. **/
	var Trust = 1;

	/** Nothing is kept between visits, and what earlier visits kept is neither used nor deleted. **/
	var Off = 2;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int` — a setter should not take any
		number — so reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):AssetCacheMode
		return cast v;

	#if cpp
	/** C++ needs the cast: the header says `wgr_asset_cache_mode_t`, not `int`. **/
	@:to extern inline function toRaw():CAssetCacheMode
		return untyped __cpp__("(wgr_asset_cache_mode_t)({0})", this);
	#end
}
