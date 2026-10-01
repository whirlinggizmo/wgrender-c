package wgr;

// wgr_logger.h

/**
	wgrender's logger, carrying the Haxe call site.

	The C calls are varargs; these hand over an already-formatted Haxe string as a
	`%s` argument rather than as the format itself, so text can never be read as a
	format directive and nothing has to be escaped.
**/
class Log {
	public static inline function setLevel(level:LogLevel):Void
		Raw.wgr_logger_set_level(level);

	/**
		`pos` is filled in by the compiler, so a call site reaches the log without the
		caller passing anything — the C macros' `__FILE__`/`__LINE__`, from Haxe.
	**/
	public static function message(level:LogLevel, msg:String, ?pos:haxe.PosInfos):Void {
		// An optional PosInfos arrives as Dynamic on hxcpp, so name the field types.
		final file:String = pos.fileName;
		final line:Int = pos.lineNumber;
		#if js
		// Logging is what you reach for when startup is going wrong, which is exactly
		// when the host module may not be attached yet.
		if (Raw.host == null) {
			js.Syntax.code("console.log({0})", '[$file:$line] $msg');
			return;
		}
		#end
		Raw.wgr_logger_message_source(level, file, line, "%s", msg);
	}

	/** Named `verbose`, not `trace`: Haxe already has a global `trace` that does
		something else entirely, and the two sitting side by side reads badly. **/
	public static inline function verbose(msg:String, ?pos:haxe.PosInfos):Void
		message(Trace, msg, pos);

	public static inline function debug(msg:String, ?pos:haxe.PosInfos):Void
		message(Debug, msg, pos);

	public static inline function info(msg:String, ?pos:haxe.PosInfos):Void
		message(Info, msg, pos);

	public static inline function warn(msg:String, ?pos:haxe.PosInfos):Void
		message(Warn, msg, pos);

	public static inline function error(msg:String, ?pos:haxe.PosInfos):Void
		message(Error, msg, pos);

	public static inline function fatal(msg:String, ?pos:haxe.PosInfos):Void
		message(Fatal, msg, pos);

	/** Without a call site, when you want wgrender's own bare line. **/
	public static inline function plain(level:LogLevel, msg:String):Void
		Raw.wgr_logger_message(level, "%s", msg);
}
