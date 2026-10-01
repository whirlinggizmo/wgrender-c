package wgr;

// wgr_text.h — immediate text in the built-in font

/** Text in wgrender's built-in font. For a TTF, use `Font`. **/
class Text {
	public static inline function draw(text:String, x:Int, y:Int, size:Int, color:Color):Void
		Raw.wgr_text_draw(text, x, y, size, color);

	/** Width in the built-in font. **/
	public static inline function measure(text:String, size:Int):Int
		return Raw.wgr_text_measure(text, size);

	public static inline function drawFps(x:Int, y:Int):Void
		Raw.wgr_text_draw_fps(x, y);

	/**
		What `Text.draw`/`measure` use, and what a none font means everywhere else
		(`Font.draw`, `Text2D`, `Text3D`). None goes back to wgrender's built-in font,
		JetBrains Mono, printable ASCII only. The default font holds its own reference.

		A handle that isn't a loaded font is refused, and wgrender says so in the log.
	**/
	public static inline function getDefaultFont():Font
		return (Raw.wgr_text_get_default_font() : Handle);

	/**
		What `Text.draw`/`measure` use, and what a none font means everywhere else
		(`Font.draw`, `Text2D`, `Text3D`). None goes back to wgrender's built-in font,
		JetBrains Mono, printable ASCII only. The default font holds its own reference.

		A handle that isn't a loaded font is refused, and wgrender says so in the log.
	**/
	public static inline function setDefaultFont(value:Font):Bool
		return Raw.wgr_text_set_default_font(value);

}
