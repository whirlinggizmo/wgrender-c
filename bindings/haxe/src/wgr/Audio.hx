package wgr;

// wgr_audio.h — the sound data resource

/**
	A sound file, loaded on create and released like any resource (`Resource`):
	`audio.getStatus()`, `audio.release()`. Up to 1 MB it's decoded to PCM as it loads;
	larger (music) keeps its encoded bytes and is decoded while it plays. `Sound`
	objects reference one by handle — including looping music, which is a `Sound`
	with `loop` set rather than a kind of its own.
**/
@:using(wgr.Audio, wgr.Resource)
abstract Audio(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(audio:Audio):Bool
		return (audio : Handle).isNone;

	/** `new Audio(...)` is `Audio.create(...)`, the same call. **/
	public inline function new(path:String)
		this = create(path);

	/**
		The audio at an asset path, loading on create: `Pending` at once, then `Ready`,
		or `Failed` in a later frame for a file that is missing, fails to download or
		won't decode; `Failed` at once for a path outside the asset root or before the
		asset layer runs. `Handle.NONE` only when there's no room for another. A sound
		playing it before it's `Ready` waits and plays from the start when it is;
		`Failed`, it stays silent.
	**/
	public static inline function create(path:String):Audio
		return (Raw.wgr_audio_create(path) : Handle);
}
