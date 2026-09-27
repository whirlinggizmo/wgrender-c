package wgr;

// wgr_scene.h

/** What `Scene.add` takes: a `Model`, `Sprite2D`, `Sprite3D`, `Text2D`, `Text3D`, `Emitter2D`,
	`Emitter3D`, `Shape2D`, `Shape3D` or `Light`. **/
@:using(wgr.SceneMember)
abstract SceneMember(Handle) to Handle {
	/** Whether there is a member here at all — `Scene.hovered` returns none for nothing. **/
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(member:SceneMember):Bool
		return (member : Handle).isNone;

	/**
		A handle wgrender handed back, as a member. Not `@:from` on purpose: going the
		other way is what the conversions below are for, and any handle at all should
		not pass for a member.
	**/
	@:allow(wgr)
	static inline function of(v:Handle):SceneMember
		return cast v;

	@:from static inline function ofModel(v:Model):SceneMember
		return cast v;

	@:from static inline function ofSprite2D(v:Sprite2D):SceneMember
		return cast v;

	@:from static inline function ofSprite3D(v:Sprite3D):SceneMember
		return cast v;

	@:from static inline function ofLight(v:Light):SceneMember
		return cast v;

	@:from static inline function ofText2D(v:Text2D):SceneMember
		return cast v;

	@:from static inline function ofText3D(v:Text3D):SceneMember
		return cast v;

	@:from static inline function ofEmitter2D(v:Emitter2D):SceneMember
		return cast v;

	@:from static inline function ofEmitter3D(v:Emitter3D):SceneMember
		return cast v;

	@:from static inline function ofShape2D(v:Shape2D):SceneMember
		return cast v;

	@:from static inline function ofShape3D(v:Shape3D):SceneMember
		return cast v;
}
