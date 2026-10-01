package wgr;

// wgr_window.h

/** Window flags, or-ed together: `Msaa4x | Resizable`. **/
@:using(wgr.WindowFlag)
enum abstract WindowFlag(Int) to Int {
	var Fullscreen = 0x00000002;
	var Resizable = 0x00000004;
	var Undecorated = 0x00000008;
	var Transparent = 0x00000010;
	var Msaa4x = 0x00000020;
	var VsyncOff = 0x00000040;
	var Hidden = 0x00000080;
	var LowDpi = 0x00002000;

	@:op(A | B)
	public static inline function or(a:WindowFlag, b:WindowFlag):WindowFlag
		return cast((a : Int) | (b : Int));
}
