package wgr;

// wgr_input.h

/**
	Sticks run -1 to 1 with y down, like the screen, outside the dead zone
	(`Input.setGamepadDeadzone`). Triggers run 0 to 1.
**/
enum abstract GamepadAxis(Int) to Int {
	var LeftX = 0;
	var LeftY = 1;
	var RightX = 2;
	var RightY = 3;
	var LeftTrigger = 4;
	var RightTrigger = 5;

	#if cpp
	/** C++ needs the cast: the header says `wgr_gamepad_axis_t`, not `int`. **/
	@:to extern inline function toRaw():CGamepadAxis
		return untyped __cpp__("(wgr_gamepad_axis_t)({0})", this);
	#end
}
