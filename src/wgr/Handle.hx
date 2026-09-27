package wgr;

// wgr_handle.h — the untyped handle every typed kind is an abstract over

/** An untyped wgrender handle (what a pick result hits); 0 is none. **/
@:using(wgr.Handle)
abstract Handle(Int) from Int to Int {
	/**
		The zero handle — "nothing", and a value you pass on purpose.

		Not only what a failed `create` returns. wgrender takes it as an argument:
		`new Model(Handle.NONE)` makes an empty model that can go into a scene straight
		away and get its mesh when the asset arrives, which is how every example here
		loads one. Assignable to any kind, so it needs no cast.
	**/
	public static final NONE:Handle = 0;

	/**
		Whether this is the zero handle. **The only test that is correct on both
		targets** — `h == 0` and `h == Handle.NONE` are not.

		On js an uninitialised field is `undefined`, and `undefined == 0` is `false`, so
		both comparisons report a handle for something never assigned. They compile, and
		they are right on hxcpp and right on js for a field that *was* assigned, which
		is what makes them worth naming here. Promoting the 0 happens at the typed
		abstract (`Int` -> `Handle` -> `Model`), so an operator overload on `Handle`
		cannot intercept it; there is nothing to fix but the habit.
	**/
	public var isNone(get, never):Bool;

	inline function get_isNone():Bool
		// On js an uninitialised static is `undefined`, and `undefined == 0` is false,
		// so a field declared without `= Handle.NONE` would claim to hold a handle.
		// `== null` catches both there; on a static target 0 is the only possibility.
		#if js
		return this == null || this == 0;
		#else
		return this == 0;
		#end

	/** What this handle refers to; `None` for a none handle. **/
	/** What this handle refers to; `None` for a none handle. **/
	public static inline function getKind(handle:Handle):HandleKind
		return HandleKind.of(Raw.wgr_handle_get_kind(handle));

	/** wgrender's C `wgr_handle_t`. **/
	@:to inline function toRaw():WgrHandle
		return this;

	public inline function toString():String
		return Std.string(this);
}
