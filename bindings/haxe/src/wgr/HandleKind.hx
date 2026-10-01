package wgr;

// wgr_handle.h

/**
	What a handle refers to, packed into its high bits. Read it with `Handle.kind`
	— mostly useful for a pick result, where the hit could be any drawable.
**/
enum abstract HandleKind(Int) to Int {
	/** The zero handle: not created yet, or creation failed. **/
	var None = 0;

	var Camera3D = 2;
	var Font = 3;
	var Texture = 4;
	var Sprite2D = 5;
	var Sprite3D = 6;
	var Model = 7;
	var Mesh = 8;
	var Sound = 9;
	var Text2D = 11;
	var Scene = 12;
	var Shape3D = 13;
	var Text3D = 14;

	/** Decoded PCM, shared by the sounds playing it. **/
	var Audio = 15;

	var Light = 16;
	var Material = 17;
	var Environment = 18;
	var Shape2D = 19;
	var Emitter3D = 20;
	var Emitter2D = 21;

	/** A custom material shader, loaded from a `.wgrshader`. **/
	var Shader = 22;

	var AssetTask = 32;

	/**
		A value wgrender handed back. It returns the C enum as an `int`, and this
		abstract is deliberately not `from Int` — nothing should mint a kind — so
		reading one back goes through here.
	**/
	@:allow(wgr)
	static inline function of(v:Int):HandleKind
		return cast v;
}
