package wgr;

// wgr_input.h

/**
	Every key at once, plus the keys and characters that arrived this frame.

	For one key, `Input.getKey` / `Input.isKeyPressed` are simpler; this is for code
	that wants the whole keyboard, or the text a frame produced.

	On js this is a view into the wasm heap, not a copy — 2324 bytes is too much to
	marshal per frame — so it is only good for the frame it was read in. Read what
	you need and let it go; don't keep one in a field.
**/
abstract KeyboardState(#if cpp CKeyboardState #else Int #end)
	from #if cpp CKeyboardState #else Int #end {
	/** The last key that went down this frame, or `Invalid` if none did. **/
	public var pressedKey(get, never):Key;

	/** The last character typed this frame as a codepoint, or 0 if none was. **/
	public var pressedChar(get, never):Int;

	/** How many keys went down this frame — at most 32; the rest are dropped. **/
	public var numPressedKeys(get, never):Int;

	/** How many characters were typed this frame — at most 32; the rest are dropped. **/
	public var numPressedChars(get, never):Int;

	@:arrayAccess public inline function get(key:Key):ButtonState
		#if cpp
		return this.keys[key];
		#else
		return KeyboardStateLayout.read(this, KeyboardStateLayout.keys + (key : Int));
		#end

	/** Went down this frame. **/
	public extern inline function isPressed(key:Key):Bool
		return get(key) == Pressed;

	/** Held, including the frame it went down. **/
	public extern inline function isDown(key:Key):Bool
		return get(key) == Pressed || get(key) == Down;

	/** Went up this frame. **/
	public extern inline function isReleased(key:Key):Bool
		return get(key) == Released;

	/** One of this frame's keys, `0 <= index < numPressedKeys`, oldest first. **/
	public extern inline function getPressedKey(index:Int):Key
		#if cpp
		return this.pressed_keys[index];
		#else
		return KeyboardStateLayout.read(this, KeyboardStateLayout.pressedKeys + index);
		#end

	/** One of this frame's characters as a codepoint, `0 <= index < numPressedChars`. **/
	public extern inline function getPressedChar(index:Int):Int
		#if cpp
		return this.pressed_chars[index];
		#else
		return KeyboardStateLayout.read(this, KeyboardStateLayout.pressedChars + index);
		#end

	/**
		This frame's characters as one string — what to append to a text field.
		Codepoints outside the basic plane are left out rather than split in half.
	**/
	public extern inline function typedText():String {
		final n = numPressedChars;
		if (n == 0)
			return "";
		final out = new StringBuf();
		for (i in 0...n) {
			final code = getPressedChar(i);
			if (code > 0 && code <= 0xFFFF)
				out.addChar(code);
		}
		return out.toString();
	}

	extern inline function get_pressedKey():Key
		#if cpp
		return this.pressed_key;
		#else
		return KeyboardStateLayout.read(this, KeyboardStateLayout.pressedKey);
		#end

	extern inline function get_pressedChar():Int
		#if cpp
		return this.pressed_char;
		#else
		return KeyboardStateLayout.read(this, KeyboardStateLayout.pressedChar);
		#end

	extern inline function get_numPressedKeys():Int
		#if cpp
		return this.num_pressed_keys;
		#else
		return KeyboardStateLayout.read(this, KeyboardStateLayout.numPressedKeys);
		#end

	extern inline function get_numPressedChars():Int
		#if cpp
		return this.num_pressed_chars;
		#else
		return KeyboardStateLayout.read(this, KeyboardStateLayout.numPressedChars);
		#end
}
