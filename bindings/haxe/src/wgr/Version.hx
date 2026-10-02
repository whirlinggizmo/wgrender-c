package wgr;

import wgr.impl.BuiltVersion;

// wgr_version.h — the library the binding was generated against, against the one running

/**
	A binding is generated from a particular wgrender's headers, and then runs against
	whatever library it is given. Those are not the same thing, and on the web they are
	not even the same download: `wgrender-host.wasm` and the guest's JS are fetched and
	cached independently, so a browser can pair a stale guest with a fresh host.

	`check` reports whether that pairing is one to trust. Major or minor apart means
	the C API may have moved under the binding; a patch apart is allowed through.
**/
class Version {
	/** The wgrender the binding was generated against. **/
	public static inline final BUILT = BuiltVersion.STRING;

	/** The running library's version, as `BUILT` spells it. **/
	public static function runtime():String
		return '${major()}.${minor()}.${patch()}';

	public static inline function major():Int
		return Raw.wgr_version_major();

	public static inline function minor():Int
		return Raw.wgr_version_minor();

	public static inline function patch():Int
		return Raw.wgr_version_patch();

	/** The full string the library reports, label and all. **/
	public static inline function runtimeLabelled():String
		return Raw.wgr_version_string();

	/** The pre-release label ("dev"), or "" for a plain release. **/
	public static inline function label():String
		return Raw.wgr_version_label();

	/** Major, minor and patch packed into one number, for comparing versions. **/
	public static inline function number():Int
		return Raw.wgr_version_number();

	/**
		True when the running library is one this binding was generated for. A false
		stops `GuestAbi.start`, so this logs at fatal rather than leaving a trace for
		someone to find later.
	**/
	public static function check():Bool {
		if (major() == BuiltVersion.MAJOR && minor() == BuiltVersion.MINOR)
			return true;
		Log.fatal('wgrender ${runtime()} does not match the $BUILT this binding was generated '
			+ 'against (${BuiltVersion.COMMIT}) — regenerate with tools/gen_raw_externs.py');
		return false;
	}
}
