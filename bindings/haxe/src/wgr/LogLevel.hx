package wgr;

// wgr_logger.h

enum abstract LogLevel(Int) to Int {
	var Trace = 0;
	var Debug = 1;
	var Info = 2;
	var Warn = 3;
	var Error = 4;
	var Fatal = 5;

	#if cpp
	/** C++ needs the cast: the header says `wgr_log_level_t`, not `int`. **/
	@:to extern inline function toRaw():CLogLevel
		return untyped __cpp__("(wgr_log_level_t)({0})", this);
	#end
}
