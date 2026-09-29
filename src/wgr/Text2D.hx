package wgr;

// wgr_text2d.h — retained text on the screen

/**
	A placed string with retained state: set its text, place and colour once and
	wgrender keeps them, instead of your code re-passing them every frame. Add it to a
	`Scene` and `Scene.draw` draws it; `draw` draws it on its own.

	It retains the state, not the geometry — the text is still shaped on every draw,
	at the same per-frame cost as `Font.draw`. What it saves is re-sending the string.

	A none `font` uses `Text.defaultFont` until one is attached, so you can create,
	place and show text before its font asset has loaded. It holds its own reference
	to the font.
**/
@:using(wgr.Text2D)
abstract Text2D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(text2D:Text2D):Bool
		return (text2D : Handle).isNone;

	/** `new Text2D(...)` is `Text2D.create(...)`, the same call. **/
	public inline function new(font:Font)
		this = create(font);

	/** `Handle.NONE` for the default font; attach a real one later with `font`. **/
	public static inline function create(font:Font):Text2D
		return (Raw.wgr_text2d_create(font) : Handle);

	public static inline function setFont(text2D:Text2D, value:Font):Bool
		return Raw.wgr_text2d_set_font(text2D, value);

	public static inline function setText(text2D:Text2D, value:String):Bool
		return Raw.wgr_text2d_set_text(text2D, value); // wgrender copies it;

	public static overload extern inline function setPosition(text2D:Text2D, value:Vec2):Bool
		return setPositionRaw(text2D, value.x, value.y);

	/** The same, without a `Vec2`. **/
	public static overload extern inline function setPosition(text2D:Text2D, x:Float, y:Float):Bool
		return setPositionRaw(text2D, x, y);

	// the C call, which the overloads forward to (README, "Calling it from cppia")
	static inline function setPositionRaw(text2D:Text2D, x:Float, y:Float):Bool
		return Raw.wgr_text2d_set_position(text2D, x, y);

	/** Where it is, as last set. **/
	public static inline function getPosition(text2D:Text2D):Vec2
		return Vec2.of(Raw.wgr_text2d_get_position(text2D));

	/** Pixel height of one line. **/
	public static inline function setFontSize(text2D:Text2D, value:Float):Bool
		return Raw.wgr_text2d_set_font_size(text2D, value);

	public static inline function getFontSize(text2D:Text2D):Float
		return Raw.wgr_text2d_get_font_size(text2D);

	public static inline function setColor(text2D:Text2D, value:Color):Bool
		return Raw.wgr_text2d_set_color(text2D, value);

	/** Wrap to this many logical pixels, between words; 0 is off (the default). **/
	public static inline function setMaxWidth(text2D:Text2D, value:Float):Bool
		return Raw.wgr_text2d_set_max_width(text2D, value);

	public static inline function isVisible(text2D:Text2D):Bool
		return Raw.wgr_text2d_is_visible(text2D);

	public static inline function setVisible(text2D:Text2D, value:Bool):Bool
		return Raw.wgr_text2d_set_visible(text2D, value);

	/** Whether a pick can hit it. Default: pickable. **/
	public static inline function isPickable(text2D:Text2D):Bool
		return Raw.wgr_text2d_is_pickable(text2D);

	/** Whether a pick can hit it. Default: pickable. **/
	public static inline function setPickable(text2D:Text2D, value:Bool):Bool
		return Raw.wgr_text2d_set_pickable(text2D, value);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function isEnabled(text2D:Text2D):Bool
		return Raw.wgr_text2d_is_enabled(text2D);

	/** Disabled: still drawn, picked and blocking the pointer, but it doesn't react. **/
	public static inline function setEnabled(text2D:Text2D, value:Bool):Bool
		return Raw.wgr_text2d_set_enabled(text2D, value);

	/** Default: the position is the block's top-left corner. **/
	public static inline function setAlign(text2D:Text2D, horizontal:AlignX, vertical:AlignY):Bool
		return Raw.wgr_text2d_set_align(text2D, horizontal, vertical);

	/** The laid-out text at its font size: x the widest line, y the lines' total height. **/
	public static inline function measure(text2D:Text2D):Vec2
		return Vec2.of(Raw.wgr_text2d_measure(text2D));

	/** Draw it now; a scene draws its members itself. **/
	public static inline function draw(text2D:Text2D):Void
		Raw.wgr_text2d_draw(text2D);

	/** Also takes it out of every scene it's in. **/
	public static inline function destroy(text2D:Text2D):Void
		Raw.wgr_text2d_destroy(text2D);
}
