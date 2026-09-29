package wgr;

// wgr_input.h

/**
	Named by position on an Xbox-style layout: `South` is A on Xbox, cross on
	PlayStation and B on Switch. Edges work like keys — since the previous frame in
	a frame callback, since the previous tick in a tick.
**/
enum abstract GamepadButton(Int) to Int {
	var South = 0;
	var East = 1;
	var West = 2;
	var North = 3;
	var LeftBumper = 4;
	var RightBumper = 5;

	/** Also reported as an axis; counts as a button past half-way. **/
	var LeftTrigger = 6;

	var RightTrigger = 7;

	/** View, select, share or minus, depending on the pad. **/
	var Back = 8;

	/** Menu, options or plus. **/
	var Start = 9;

	/** The logo button. **/
	var Guide = 10;

	var LeftStick = 11;
	var RightStick = 12;
	var DpadUp = 13;
	var DpadDown = 14;
	var DpadLeft = 15;
	var DpadRight = 16;

	#if cpp
	/** C++ needs the cast: the header says `wgr_gamepad_button_t`, not `int`. **/
	@:to extern inline function toRaw():CGamepadButton
		return untyped __cpp__("(wgr_gamepad_button_t)({0})", this);
	#end
}
