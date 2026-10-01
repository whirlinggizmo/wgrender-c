package wgr;

// wgr_audio.h — the sound data resource

/**
	A loaded sound file: decoded PCM, reference counted and shared. `Sound` objects
	reference one by handle — including looping music, which is a `Sound` with
	`loop` set rather than a kind of its own.
**/
@:using(wgr.Audio)
abstract Audio(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(audio:Audio):Bool
		return (audio : Handle).isNone;

	/** `new Audio(...)` is `Audio.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	public static inline function create(path:String):Audio
		return (Raw.wgr_audio_create(path) : Handle);

	/** Drop this reference; the data goes when the last one does. **/
	public static inline function release(audio:Audio):Void
		Raw.wgr_audio_release(audio);
}
