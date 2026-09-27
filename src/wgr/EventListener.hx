package wgr;

// wgr_event.h

/** What `Event.on` hands back, for `Event.off` to take. A zero token never listened. **/
@:using(wgr.EventListener)
abstract EventListener(Int) from Int to Int {
	/**
		Whether this token never listened.

		`token == 0` is wrong on js for a field that was never assigned — `undefined`
		is not `0` — so the comparison lives here rather than at each call site.
	**/
	public static inline function isNone(listener:EventListener):Bool
		#if js
		return (listener : Int) == null || (listener : Int) == 0;
		#else
		return (listener : Int) == 0;
		#end
}
