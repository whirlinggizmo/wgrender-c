package wgr;

// wgr_color.h

/** A packed 0xRRGGBBAA color, by value. **/
@:using(wgr.Color)
abstract Color(Int) from Int to Int {
	public static inline var LIGHTGRAY:Color = cast 0xC8C8C8FF;
	public static inline var GRAY:Color = cast 0x828282FF;
	public static inline var DARKGRAY:Color = cast 0x505050FF;
	public static inline var YELLOW:Color = cast 0xFFFF00FF;
	public static inline var GOLD:Color = cast 0xFFCB00FF;
	public static inline var ORANGE:Color = cast 0xFFA100FF;
	public static inline var PINK:Color = cast 0xFF6DC2FF;
	public static inline var RED:Color = cast 0xE62937FF;
	public static inline var MAROON:Color = cast 0xBE212DFF;
	public static inline var GREEN:Color = cast 0x00E430FF;
	public static inline var LIME:Color = cast 0x009E2FFF;
	public static inline var DARKGREEN:Color = cast 0x00752CFF;
	public static inline var SKYBLUE:Color = cast 0x66BFFFFF;
	public static inline var BLUE:Color = cast 0x0079F1FF;
	public static inline var DARKBLUE:Color = cast 0x0052ACFF;
	public static inline var PURPLE:Color = cast 0xC87AFFFF;
	public static inline var VIOLET:Color = cast 0x873CBEFF;
	public static inline var DARKPURPLE:Color = cast 0x701F7EFF;
	public static inline var BEIGE:Color = cast 0xD3B083FF;
	public static inline var BROWN:Color = cast 0x7F6A4FFF;
	public static inline var DARKBROWN:Color = cast 0x4C3F2FFF;
	public static inline var WHITE:Color = cast 0xFFFFFFFF;
	public static inline var BLACK:Color = cast 0x000000FF;
	/** Fully transparent. Note that this is not "unset": a tint that changes
		nothing is `WHITE`. **/
	public static inline var BLANK:Color = cast 0x00000000;
	public static inline var MAGENTA:Color = cast 0xFF00FFFF;
	public static inline var RAYWHITE:Color = cast 0xF5F5F5FF;






	/** Components 0..255, clamped. They saturate rather than wrap into the next channel. **/
	public static inline function rgba(r:Int, g:Int, b:Int, a:Int = 255):Color
		return Raw.wgr_color_rgba(r, g, b, a);

	/** The same from 0..1, rounded to the nearest 8-bit step. **/
	public static inline function rgbaf(r:Float, g:Float, b:Float, a:Float = 1):Color
		return Raw.wgr_color_rgbaf(r, g, b, a);

	/** Red, 0..255. **/
	public static inline function getRed(color:Color):Int
		return Raw.wgr_color_get_red(color);

	/** Green, 0..255. **/
	public static inline function getGreen(color:Color):Int
		return Raw.wgr_color_get_green(color);

	/** Blue, 0..255. **/
	public static inline function getBlue(color:Color):Int
		return Raw.wgr_color_get_blue(color);

	/** Alpha, 0..255. Note that 0 is transparent, not "unset". **/
	public static inline function getAlpha(color:Color):Int
		return Raw.wgr_color_get_alpha(color);

	/** The same color at another alpha, 0..255. **/
	public static inline function withAlpha(color:Color, alpha:Int):Color
		return Raw.wgr_color_with_alpha(color, alpha);

	/** A straight-line blend, component by component; `t` is clamped to 0..1. **/
	public static inline function lerp(from:Color, to:Color, t:Float):Color
		return Raw.wgr_color_lerp(from, to, t);

	/** wgrender's C `wgr_color_t`. **/
	@:to inline function toRaw():WgrColor
		return this;
}
