package wgr;

// wgr_scene.h — what is drawn, and picking against it

@:using(wgr.Scene)
abstract Scene(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(scene:Scene):Bool
		return (scene : Handle).isNone;

	/** `new Scene()` is `Scene.create()`, the same call. **/
	public inline function new()
		this = create();

	public static inline function create():Scene
		return (Raw.wgr_scene_create() : Handle);

	/**
		One of the seven wgrender setters that return nothing, so this returns nothing
		either rather than inventing a `true`.
	**/
	public static inline function setActiveCamera(scene:Scene, camera:Camera3D):Void
		Raw.wgr_scene_set_active_camera(scene, camera);

	/**
		Whether the scene tracks the pointer. An interactive scene picks under the
		pointer once per frame, before the frame's ticks, against where its members were
		last drawn, and keeps hover and press per member: 2D members first (topmost),
		then the nearest 3D one. Changing this resets that state. Default: off.
	**/
	public static inline function isInteractive(scene:Scene):Bool
		return Raw.wgr_scene_is_interactive(scene);

	/**
		Whether the scene tracks the pointer. An interactive scene picks under the
		pointer once per frame, before the frame's ticks, against where its members were
		last drawn, and keeps hover and press per member: 2D members first (topmost),
		then the nearest 3D one. Changing this resets that state. Default: off.
	**/
	public static inline function setInteractive(scene:Scene, value:Bool):Bool
		return Raw.wgr_scene_set_interactive(scene, value);

	/**
		Skip members the camera can't see, on by default. Each member's bounds are
		tested against the view before it is submitted, which is far cheaper than
		drawing it. Turn it off to see everything submitted — when checking whether a
		drawable's bounds are right. Members without bounds (2D ones) are never culled.
	**/
	public static inline function isCulling(scene:Scene):Bool
		return Raw.wgr_scene_is_culling(scene);

	/**
		Skip members the camera can't see, on by default. Each member's bounds are
		tested against the view before it is submitted, which is far cheaper than
		drawing it. Turn it off to see everything submitted — when checking whether a
		drawable's bounds are right. Members without bounds (2D ones) are never culled.
	**/
	public static inline function setCulling(scene:Scene, value:Bool):Bool
		return Raw.wgr_scene_set_culling(scene, value);

	/** The topmost member under the pointer, enabled or not; none if there isn't one. **/
	public static inline function getHovered(scene:Scene):SceneMember
		return SceneMember.of(Raw.wgr_scene_get_hovered(scene));

	public static inline function add(scene:Scene, member:SceneMember, layer:Int = 0):Bool
		return Raw.wgr_scene_add(scene, member, layer);

	/** Move a member to another layer. **/
	public static inline function setLayer(scene:Scene, member:SceneMember, layer:Int):Bool
		return Raw.wgr_scene_set_layer(scene, member, layer);

	/** Take it out; it keeps existing, and so does anything else holding it. **/
	public static inline function remove(scene:Scene, member:SceneMember):Bool
		return Raw.wgr_scene_remove(scene, member);

	/** Take every member out. The members themselves are untouched. **/
	public static inline function clear(scene:Scene):Void
		Raw.wgr_scene_clear(scene);

	/**
		Clip one layer's 2D members to a screen rectangle: what falls outside isn't
		drawn and isn't picked, which is what a scrolling list or a panel with content
		needs. A zero width or height removes the layer's rectangle. 3D members are
		never clipped. The rectangles belong to the scene, so they outlive `clear`; at
		most 8 layers per scene.
	**/
	public static inline function setClip(scene:Scene, layer:Int, x:Float, y:Float, width:Float, height:Float):Bool
		return Raw.wgr_scene_set_clip(scene, layer, x, y, width, height);

	public static inline function setAmbient(scene:Scene, color:Color, intensity:Float):Bool
		return Raw.wgr_scene_set_ambient(scene, color, intensity);

	/**
		Image-based lighting: reflections and diffuse light from `environment`, on top
		of lights and ambient, for the scene's PBR models. `intensity` scales it (1 is
		as authored) and `rotation` (radians) turns it around the world up axis. A none
		handle removes it. The scene holds its own reference.
	**/
	public static inline function setEnvironment(scene:Scene, environment:Environment, intensity:Float = 1, rotation:Float = 0):Bool
		return Raw.wgr_scene_set_environment(scene, environment, intensity, rotation);

	/**
		Draw `environment` behind everything (a skybox), with the scene's environment
		intensity and rotation when it is the same environment, else plain. `blur` runs
		0 (sharp) to 1 (fully blurred). A none handle removes it.
	**/
	public static inline function setBackground(scene:Scene, environment:Environment, blur:Float = 0):Bool
		return Raw.wgr_scene_set_background(scene, environment, blur);

	/** `exposure` is in stops: +1 doubles the brightness. Default: `Neutral`, 0. **/
	public static inline function setTonemap(scene:Scene, tonemap:Tonemap, exposure:Float = 0):Bool
		return Raw.wgr_scene_set_tonemap(scene, tonemap, exposure);

	public static inline function draw(scene:Scene):Void
		Raw.wgr_scene_draw(scene);

	/**
		Whether the pointer is over `member`: `Pressed` when it came over it, `Down`
		while it is, `Released` when it left. Needs `interactive`.
	**/
	public static inline function getHover(scene:Scene, member:SceneMember):ButtonState
		return Raw.wgr_scene_get_hover(scene, member);

	/**
		The primary button, for a press that started on `member`: `Pressed` when it went
		down, `Down` while held — including after the pointer moved off — and `Released`
		when let go. Needs `interactive`.
	**/
	public static inline function getPress(scene:Scene, member:SceneMember):ButtonState
		return Raw.wgr_scene_get_press(scene, member);

	/** Released while still over the member it was pressed on. Needs `interactive`. **/
	public static inline function isClicked(scene:Scene, member:SceneMember):Bool
		return Raw.wgr_scene_is_clicked(scene, member);

	/**
		What is under (`x`, `y`) in screen space. A none `camera` uses the scene's
		active one. Compare the result with typed handles: `pick.handle == model`.
	**/
	public static inline function pick(scene:Scene, x:Float, y:Float, ?camera:Camera3D):PickResult
		return Scene.toPickResult(Raw.wgr_scene_pick(scene, camera == null ? 0 : (camera : Handle), x, y));

	/** A scene doesn't own its members; destroying it leaves them alone. **/
	public static inline function destroy(scene:Scene):Void
		Raw.wgr_scene_destroy(scene);

	@:allow(wgr)
	static function toPickResult(r:#if cpp CPickResult #else PickResult #end):PickResult {
		#if cpp
		return new PickResult(r.hit, (r.handle : Int), r.distance, Vec3.of(r.point_local), Vec3.of(r.point_world),
			Vec3.of(r.normal_local), Vec3.of(r.normal_world));
		#else
		return r;
		#end
	}
}
