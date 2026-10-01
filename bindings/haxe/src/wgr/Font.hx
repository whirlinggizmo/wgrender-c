package wgr;

// wgr_font.h — the typeface resource

/** A loaded typeface. Fonts are sized per draw call, so one handle serves any size. **/
@:using(wgr.Font)
abstract Font(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(font:Font):Bool
		return (font : Handle).isNone;

	/** `new Font(...)` is `Font.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	public static inline function create(path:String):Font
		return (Raw.wgr_font_create(path) : Handle);

	public static inline function draw(font:Font, text:String, x:Float, y:Float, size:Float, color:Color):Void
		Raw.wgr_text_draw_ex(font, text, x, y, size, color);

	public static inline function measure(font:Font, text:String, size:Float):Vec2
		return Vec2.of(Raw.wgr_text_measure_ex(font, text, size));

	/** The frame rate, in this font; a none font draws in the built-in one. **/
	public static inline function drawFps(font:Font, x:Float, y:Float, size:Float, color:Color):Void
		Raw.wgr_text_draw_fps_ex(font, x, y, size, color);

	/** Once at a 3D point, facing the camera; `size` is line height in world units. **/
	public static inline function draw3D(font:Font, text:String, position:Vec3, size:Float, color:Color):Void
		Raw.wgr_text_draw_3d(font, text, position.x, position.y, position.z, size, color);

	/** Drop this reference; the font goes when the last one does. **/
	public static inline function release(font:Font):Void
		Raw.wgr_font_release(font);
}
