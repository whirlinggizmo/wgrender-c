package wgr;

// wgr_camera3d.h

@:using(wgr.Camera3D)
abstract Camera3D(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(camera3D:Camera3D):Bool
		return (camera3D : Handle).isNone;

	/** `new Camera3D(...)` is `Camera3D.create(...)`, the same call. **/
	public inline function new(projection:Projection = Perspective)
		this = create(projection);

	/** Perspective's default fov is pi/4 (45 degrees). **/
	public static inline function create(projection:Projection = Perspective):Camera3D
		return (Raw.wgr_camera3d_create(projection) : Handle);

	/** The one scenes fall back to. Assign with `Camera3D.setActive`. **/
	public static inline function getActive():Camera3D
		return (Raw.wgr_camera3d_get_active() : Handle);

	/** The camera wgrender makes for you, which needs no creating or destroying. **/
	public static inline function getDefault():Camera3D
		return (Raw.wgr_camera3d_get_default() : Handle);

	/** Make this the camera scenes without one of their own use. **/
	public static inline function setActive(camera3D:Camera3D):Bool
		return Raw.wgr_camera3d_set_active(camera3D);

	/** Perspective vs orthographic; `fov` and `orthoHeight` each apply to one. **/
	public static inline function getProjection(camera3D:Camera3D):Projection
		return Projection.of(Raw.wgr_camera3d_get_projection(camera3D));

	/** Perspective vs orthographic; `fov` and `orthoHeight` each apply to one. **/
	public static inline function setProjection(camera3D:Camera3D, value:Projection):Bool
		return Raw.wgr_camera3d_set_projection(camera3D, value);

	/** Vertical field of view in radians, `0 < fov < pi`. Perspective only. **/
	public static inline function getFov(camera3D:Camera3D):Float
		return Raw.wgr_camera3d_get_fov(camera3D);

	/** Vertical field of view in radians, `0 < fov < pi`. Perspective only. **/
	public static inline function setFov(camera3D:Camera3D, value:Float):Bool
		return Raw.wgr_camera3d_set_fov(camera3D, value);

	/** Full visible height in world units, `> 0`; width follows the window. Ortho only. **/
	public static inline function getOrthoHeight(camera3D:Camera3D):Float
		return Raw.wgr_camera3d_get_ortho_height(camera3D);

	/** Full visible height in world units, `> 0`; width follows the window. Ortho only. **/
	public static inline function setOrthoHeight(camera3D:Camera3D, value:Float):Bool
		return Raw.wgr_camera3d_set_ortho_height(camera3D, value);

	public static inline function setView(camera3D:Camera3D, position:Vec3, target:Vec3, ?up:Vec3):Bool {
		final u = up != null ? up : Camera3DDefaults.UP;
		return Raw.wgr_camera3d_set_view(camera3D, position.x, position.y, position.z, target.x, target.y, target.z, u.x,
			u.y, u.z);
	}

	/** Scenes using it fall back to the active camera. **/
	public static inline function destroy(camera3D:Camera3D):Void
		Raw.wgr_camera3d_destroy(camera3D);
}

@:noCompletion
class Camera3DDefaults {
	public static final UP = new Vec3(0, 1, 0);
}
