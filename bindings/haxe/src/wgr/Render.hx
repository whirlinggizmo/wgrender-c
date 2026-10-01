package wgr;

// wgr_render.h — the frame

class Render {
	public static inline function beginFrame():Void
		Raw.wgr_render_begin_frame();

	public static inline function endFrame():Void
		Raw.wgr_render_end_frame();

	public static inline function clearBackground(color:Color):Void
		Raw.wgr_render_clear_background(color);

	/** Immediate 3D primitives (`Shape3D`) draw between these, in call order. **/
	public static inline function beginMode3D():Void
		Raw.wgr_render_begin_mode_3d();

	public static inline function endMode3D():Void
		Raw.wgr_render_end_mode_3d();

	/** Screen space in logical pixels, which `begin` already set up — only needed after 3D. **/
	public static inline function beginMode2D():Void
		Raw.wgr_render_begin_mode_2d();

	public static inline function endMode2D():Void
		Raw.wgr_render_end_mode_2d();

	/**
		Clip drawing to a rectangle until the matching `popClip`. Clips nest — each push
		intersects the one it's inside, so a scroll area inside a panel stays inside the
		panel. A zero width or height clips everything away. Up to 32 deep; every push
		should be popped within the frame.
	**/
	public static inline function pushClip(x:Float, y:Float, width:Float, height:Float):Void
		Raw.wgr_render_push_clip(x, y, width, height);

	public static inline function popClip():Void
		Raw.wgr_render_pop_clip();

	/**
		Draw into a render target (`Texture.createTarget`) instead of the screen, until
		`endTexture`. Everything works inside: clear, 2D, 3D, scenes, models, text. 2D
		coordinates are the target's pixels. Not nestable, and a target can't be used as
		a texture inside its own pass.
	**/
	public static inline function beginTexture(target:Texture):Bool
		return Raw.wgr_render_begin_texture(target);

	public static inline function endTexture():Void
		Raw.wgr_render_end_texture();

	/**
		Post-processing: the frame is drawn into a texture and each effect redraws it, in
		the order added, the last onto the screen. `material` must be a custom material
		whose shader is a screen effect; a surface material is refused. Up to 8. The
		chain holds a reference to each. Call outside `begin`/`end`.
	**/
	public static inline function addEffect(material:Material):Bool
		return Raw.wgr_render_add_effect(material);

	/** Drop the whole chain, and its references to the materials. **/
	public static inline function clearEffects():Void
		Raw.wgr_render_clear_effects();

	public static inline function effectCount():Int
		return Raw.wgr_render_effect_count();
}
