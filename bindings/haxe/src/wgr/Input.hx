package wgr;

// wgr_input.h — mouse and keyboard

class Input {
	/** Up to four pads at once; a pad keeps its slot while it is connected. **/
	public static inline var MAX_GAMEPADS = 4;

	/** Fingers tracked at once. **/
	public static inline var MAX_TOUCHES = 8;

	/** In logical pixels, top-left origin. **/
	public static inline function getMousePosition(?result:Vec2):Vec2
		return Vec2.of(Raw.wgr_input_get_mouse_position(), result);

	/** How far it moved this frame (or tick). **/
	public static inline function getMouseDelta(?result:Vec2):Vec2
		return Vec2.of(Raw.wgr_input_get_mouse_delta(), result);

	/** This frame's (or tick's) wheel: y vertical, x horizontal; fractional on trackpads. **/
	public static inline function getMouseWheel(?result:Vec2):Vec2
		return Vec2.of(Raw.wgr_input_get_mouse_wheel(), result);

	/**
		Whether game controls — camera drags, 3D selection, hotkeys — should leave the
		pointer alone because a UI has it. Advisory: wgrender keeps reporting input, and
		game code is what checks this first.

		It is captured while a press that started on a 2D member of an interactive scene
		is held, and while the game's UI says so. A UI's own captures are sticky: set
		one every frame from the UI's hit-testing, and clear it when that stops being
		true. A UI lays out in the frame callback, after that frame's ticks, so a tick
		sees the previous frame's value.
	**/
	public static inline function isPointerCaptured():Bool
		return Raw.wgr_input_is_pointer_captured();

	/**
		Whether game controls — camera drags, 3D selection, hotkeys — should leave the
		pointer alone because a UI has it. Advisory: wgrender keeps reporting input, and
		game code is what checks this first.

		It is captured while a press that started on a 2D member of an interactive scene
		is held, and while the game's UI says so. A UI's own captures are sticky: set
		one every frame from the UI's hit-testing, and clear it when that stops being
		true. A UI lays out in the frame callback, after that frame's ticks, so a tick
		sees the previous frame's value.
	**/
	public static inline function setPointerCaptured(value:Bool):Void
		Raw.wgr_input_set_pointer_captured(value);

	/** The same for the keyboard — a text field with focus, say. **/
	public static inline function isKeyboardCaptured():Bool
		return Raw.wgr_input_is_keyboard_captured();

	/** The same for the keyboard — a text field with focus, say. **/
	public static inline function setKeyboardCaptured(value:Bool):Void
		Raw.wgr_input_set_keyboard_captured(value);

	/** Fingers down, plus those lifted this frame (or tick). **/
	public static inline function getTouchCount():Int
		return Raw.wgr_input_get_touch_count();

	/** Hide the cursor and keep it in the window — mouse-look. `mouseDelta` still moves. **/
	public static inline function captureCursor():Void
		Raw.wgr_input_capture_cursor();

	public static inline function releaseCursor():Void
		Raw.wgr_input_release_cursor();

	public static inline function getMouseButton(button:MouseButton):ButtonState
		return Raw.wgr_input_get_mouse_button(button);

	/** Went down this frame. **/
	public static inline function isMouseButtonPressed(button:MouseButton):Bool
		return getMouseButton(button) == Pressed;

	/** Held, including the frame it went down. **/
	public static inline function isMouseButtonDown(button:MouseButton):Bool
		return getMouseButton(button) == Pressed || getMouseButton(button) == Down;

	/** Went up this frame. **/
	public static inline function isMouseButtonReleased(button:MouseButton):Bool
		return getMouseButton(button) == Released;

	/**
		One finger, `0 <= index < touchCount`, oldest first.

		The first finger also drives the pointer — its position and the left button — so
		UI and scene interaction work by touch. A second finger cancels that press: the
		pointer is released off-screen at (-1, -1), so nothing under it is clicked, and
		it stays up until every finger has lifted.
	**/
	public static function getTouch(index:Int):Touch {
		#if cpp
		final t = Raw.wgr_input_get_touch(index);
		return new Touch(t.id, t.x, t.y, t.dx, t.dy, t.state);
		#else
		return Raw.wgr_input_get_touch(index);
		#end
	}

	/** The first two fingers, while both are down: their midpoint, pan, pinch and twist. **/
	public static function getTouchGesture():TouchGesture {
		#if cpp
		final g = Raw.wgr_input_get_touch_gesture();
		return new TouchGesture(g.active, g.x, g.y, g.dx, g.dy, g.scale, g.rotation);
		#else
		return Raw.wgr_input_get_touch_gesture();
		#end
	}

	/**
		Pad `0 <= pad < MAX_GAMEPADS`. A pad keeps its slot while connected and a new one
		takes the lowest free slot, so pad 0 stays "player 1" until it is unplugged.

		On the web the browser only lists a pad after one of its buttons is pressed on
		the page. macOS has no gamepad support yet.
	**/
	public static inline function isGamepadConnected(pad:Int):Bool
		return Raw.wgr_input_is_gamepad_connected(pad);

	/** The pad's name, or "" when nothing is in that slot. **/
	public static inline function getGamepadName(pad:Int):String
		return Raw.wgr_input_get_gamepad_name(pad);

	public static inline function getGamepadButton(pad:Int, button:GamepadButton):ButtonState
		return Raw.wgr_input_get_gamepad_button(pad, button);

	/** Went down this frame. **/
	public static inline function isGamepadButtonPressed(pad:Int, button:GamepadButton):Bool
		return getGamepadButton(pad, button) == Pressed;

	/** Held, including the frame it went down. **/
	public static inline function isGamepadButtonDown(pad:Int, button:GamepadButton):Bool
		return getGamepadButton(pad, button) == Pressed || getGamepadButton(pad, button) == Down;

	/** Went up this frame. **/
	public static inline function isGamepadButtonReleased(pad:Int, button:GamepadButton):Bool
		return getGamepadButton(pad, button) == Released;

	/** Sticks -1 to 1 with y down; triggers 0 to 1. **/
	public static inline function getGamepadAxis(pad:Int, axis:GamepadAxis):Float
		return Raw.wgr_input_get_gamepad_axis(pad, axis);

	/**
		How far a stick has to leave the middle to count, 0 to 0.9; 0.15 by default.
		Past it the values rescale so they still reach 1 at the edge. A radius outside
		that range is refused.
	**/
	public static inline function setGamepadDeadzone(radius:Float):Bool
		return Raw.wgr_input_set_gamepad_deadzone(radius);

	public static function getMouseState():MouseState {
		// hxcpp unpacks the C struct; Raw.js.hx reads it out of the heap for us.
		#if cpp
		final m = Raw.wgr_input_get_mouse_state();
		return new MouseState(m.x, m.y, m.wheel, m.wheel_x, m.left, m.right, m.middle,
			[m.buttons[0], m.buttons[1], m.buttons[2]], m.dx, m.dy);
		#else
		return Raw.wgr_input_get_mouse_state();
		#end
	}

	public static inline function getKey(key:Key):ButtonState
		return Raw.wgr_input_get_key(key);

	/** Went down this frame. **/
	public static inline function isKeyPressed(key:Key):Bool
		return getKey(key) == Pressed;

	/** Held, including the frame it went down. **/
	public static inline function isKeyDown(key:Key):Bool
		return getKey(key) == Pressed || getKey(key) == Down;

	/** Went up this frame. **/
	public static inline function isKeyReleased(key:Key):Bool
		return getKey(key) == Released;

	/**
		The whole keyboard at once. For one key, `getKey` / `isKeyPressed` are simpler.
		On js the result is a heap view good only for this frame — see `KeyboardState`.
	**/
	public static extern inline function getKeyboardState():KeyboardState
		return Raw.wgr_input_get_keyboard_state();
}
