package wgr;

// wgr_input.h

enum abstract ButtonState(Int) from Int to Int {
	/** Not held. **/
	var Up = 0;

	/** Went down this frame. **/
	var Pressed = 1;

	/** Held. **/
	var Down = 2;

	/** Went up this frame. **/
	var Released = 3;
}
