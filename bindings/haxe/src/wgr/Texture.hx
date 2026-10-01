package wgr;

// wgr_texture.h — the image resource

/** A loaded image: reference counted, shared. **/
@:using(wgr.Texture)
abstract Texture(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(texture:Texture):Bool
		return (texture : Handle).isNone;

	/** `new Texture(...)` is `Texture.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	/**
		Load an image. A path ending `.ktx` names a texture compressed for GPUs: the
		first of `name.bc7.ktx`, `name.astc.ktx` or `name.etc2.ktx` this GPU can sample
		is loaded, and `name.png` when none of them can. On the web only the chosen one
		downloads. Name the plain `name.ktx` — naming a variant outright loads that one,
		with no fallback.
	**/
	public static inline function create(path:String):Texture
		return (Raw.wgr_texture_create(path) : Handle);

	/**
		A texture to draw into: `width` x `height` pixels, cleared to transparent black
		each time. Draw into it between `Render.beginTexture` and `Render.endTexture`,
		then use it like any other. Matches the screen's format and MSAA; no mipmaps.
	**/
	public static inline function createTarget(width:Int, height:Int):Texture
		return (Raw.wgr_texture_create_target(width, height) : Handle);

	/** The 1x1 white texture, for a material or sprite that wants no image. **/
	public static inline function getDefault():Texture
		return (Raw.wgr_texture_get_default() : Handle);

	/**
		Stands in for a texture that failed to load — a glTF file's missing image, say,
		which still loads the model, with a warning. Built in: a magenta and black
		checker. Setting one takes a reference; set `Handle.NONE` to restore the
		built-in. Only affects resources loaded after the call.
	**/
	public static inline function getPlaceholder():Texture
		return (Raw.wgr_texture_get_placeholder() : Handle);

	/**
		Stands in for a texture that failed to load — a glTF file's missing image, say,
		which still loads the model, with a warning. Built in: a magenta and black
		checker. Setting one takes a reference; set `Handle.NONE` to restore the
		built-in. Only affects resources loaded after the call.
	**/
	public static inline function setPlaceholder(value:Texture):Bool
		return Raw.wgr_texture_set_placeholder(value);

	/** Its size in pixels. **/
	public static inline function getSize(texture:Texture):Vec2
		return Vec2.of(Raw.wgr_texture_get_size(texture));

	/**
		Draw it once, axis-aligned, top-left at (`x`, `y`) in logical pixels — no object
		needed. A `width` or `height` at or below 0 uses the texture's own. Outside 3D
		mode, in call order. For rotation, a source region or picking, use `Sprite2D`.
	**/
	public static inline function draw(texture:Texture, x:Float, y:Float, width:Float = 0, height:Float = 0, tint:Color = Color.WHITE):Void
		Raw.wgr_texture_draw(texture, x, y, width, height, tint);

	/**
		A region of it — `source` in texture pixels, a zero size meaning all of it —
		drawn into the rectangle at (`x`, `y`). For icons and panels cut from an atlas.
	**/
	public static inline function drawRegion(texture:Texture, sourceX:Float, sourceY:Float, sourceWidth:Float, sourceHeight:Float, x:Float,
			y:Float, width:Float = 0, height:Float = 0, tint:Color = Color.WHITE):Void
		Raw.wgr_texture_draw_ex(texture, sourceX, sourceY, sourceWidth, sourceHeight, x, y, width, height, tint);

	/**
		The same region, nine-sliced: the borders keep their size, the edges stretch
		along one axis and the middle along both — a skinned panel or button at any
		size. Borders that don't fit shrink to fit; all four at 0 draws the plain region.
	**/
	public static inline function drawNineSlice(texture:Texture, sourceX:Float, sourceY:Float, sourceWidth:Float, sourceHeight:Float,
			left:Float, top:Float, right:Float, bottom:Float, x:Float, y:Float, width:Float = 0, height:Float = 0,
			tint:Color = Color.WHITE):Void
		Raw.wgr_texture_draw_nine_slice(texture, sourceX, sourceY, sourceWidth, sourceHeight, left, top, right, bottom, x,
			y, width, height, tint);

	/** How this texture repeats and filters wherever it is used. **/
	public static inline function setSampling(texture:Texture, wrapU:TextureWrap, wrapV:TextureWrap, filter:TextureFilter):Bool
		return Raw.wgr_texture_set_sampling(texture, wrapU, wrapV, filter);

	/** Drop this reference; the data goes when the last one does. **/
	public static inline function release(texture:Texture):Void
		Raw.wgr_texture_release(texture);
}
