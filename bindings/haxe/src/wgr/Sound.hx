package wgr;

// wgr_sound.h — a playable instance of an Audio

/** A playing (or playable) instance of an `Audio`, with its own state. **/
@:using(wgr.Sound)
abstract Sound(Handle) from Handle to Handle {
	@:to inline function toRaw():WgrHandle
		return (this : Int);

	/** Whether this refers to nothing; tolerates a field never assigned, on js. **/
	public static inline function isNone(sound:Sound):Bool
		return (sound : Handle).isNone;

	/** `new Sound(...)` is `Sound.create(...)`, the same call. **/
	public inline function new(audio:Audio)
		this = create(audio);

	/** The sound takes its own reference to `audio`. **/
	public static inline function create(audio:Audio):Sound
		return (Raw.wgr_sound_create(audio) : Handle);

	/** 0 silent, 1 as recorded. **/
	public static inline function setVolume(sound:Sound, value:Float):Bool
		return Raw.wgr_sound_set_volume(sound, value);

	/** Playback rate: 1 as recorded, 2 an octave up. Changes the speed too. **/
	public static inline function setPitch(sound:Sound, value:Float):Bool
		return Raw.wgr_sound_set_pitch(sound, value);

	/** -1 hard left, 0 centered, 1 hard right. **/
	public static inline function setPan(sound:Sound, value:Float):Bool
		return Raw.wgr_sound_set_pan(sound, value);

	/** Whether it is sounding right now. **/
	public static inline function isPlaying(sound:Sound):Bool
		return Raw.wgr_sound_is_playing(sound);

	public static inline function setLoop(sound:Sound, value:Bool):Bool
		return Raw.wgr_sound_set_loop(sound, value);

	public static inline function play(sound:Sound):Bool
		return Raw.wgr_sound_play(sound);

	/** Stop, keeping the position, so `resume` picks up where it left off. **/
	public static inline function pause(sound:Sound):Bool
		return Raw.wgr_sound_pause(sound);

	/** Play on from the current position. **/
	public static inline function resume(sound:Sound):Bool
		return Raw.wgr_sound_resume(sound);

	/** Stop and rewind. **/
	public static inline function stop(sound:Sound):Bool
		return Raw.wgr_sound_stop(sound);

	/** The sound takes its own reference; a none handle leaves it nothing to play. **/
	public static inline function setAudio(sound:Sound, audio:Audio):Bool
		return Raw.wgr_sound_set_audio(sound, audio);

	/**
		The `Audio` it plays (the sound holds the reference), or none. Its status says
		whether the sound is waiting for it to load.
	**/
	public static inline function getAudio(sound:Sound):Audio
		return (Raw.wgr_sound_get_audio(sound) : Handle);

	/** Objects are private, so they're destroyed; resources are shared and released. **/
	public static inline function destroy(sound:Sound):Void
		Raw.wgr_sound_destroy(sound);
}
