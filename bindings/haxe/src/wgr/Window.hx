package wgr;

// wgr_window.h — the window, and the monitors it can sit on

/**
	The window is owned by the runtime: `Wgr.initValues` configures it, `Wgr.run`
	opens it, and the cleanup callback runs as it closes. Everything here adjusts it
	while it is open.

	Sizes are logical pixels; positions are the desktop's coordinates with a top-left
	origin. A call returns false where the platform can't do what was asked, and logs
	why once:

	- web: the canvas is the window. Setting its size works unless the page's CSS
	  overrides it; there is no position, and one monitor — the screen.
	- Linux under XWayland: compositors usually ignore a program moving its own
	  window, and may ignore resizing, though the call still succeeds.
	- fullscreen on the web only takes effect during a user gesture.
**/
class Window {
	public static inline function setTitle(value:String):Void
		Raw.wgr_window_set_title(value);

	/** Whether the user has asked to close it — the X, or the window manager. **/
	public static inline function closeRequested():Bool
		return Raw.wgr_window_close_requested() != 0;

	/** The drawable area in logical pixels. **/
	public static inline function getScreenSize():Vec2
		return Vec2.of(Raw.wgr_window_get_screen_size());

	/** Where it sits on the desktop; (0, 0) where there is no such thing. **/
	public static inline function getPosition():Vec2
		return Vec2.of(Raw.wgr_window_get_position());

	/**
		Whether the window is fullscreen *now*. Setting it asks; it does not arrive on
		the same frame.

		The web supports it -- sokol calls `canvas.requestFullscreen()`, and the browser
		allows that because the transient activation from the key or click that led here
		is still live a frame later. But the change comes back as a `fullscreenchange`
		event, so reading this on the frame it was set still gives the old value. Read
		it on a later frame, or not at all; nothing needs polling.

		Read-only, and deliberately: asking goes through `requestFullscreen`, which
		hands back what wgrender said. A setter here would have to swallow that -- Haxe
		makes `set_fullscreen` return `Bool` meaning the assigned value, not the
		outcome -- and the outcome is the whole question. `hasFullscreen` says whether
		there is anything to ask for.
	**/
	public static inline function isFullscreen():Bool
		return Raw.wgr_window_is_fullscreen();

	/**
		Ask to enter or leave fullscreen. `false` is exactly what `hasFullscreen`
		reports -- there was nothing to request; `true` means the request was made,
		which is not the same as being fullscreen: read `fullscreen` on a later frame
		for that. Named for what it does, because a `setX` returning `Bool` everywhere
		else here means the thing happened.
	**/
	public static inline function requestFullscreen(fullscreen:Bool):Bool
		return Raw.wgr_window_request_fullscreen(fullscreen);

	/**
		Whether this platform has fullscreen at all: true on the desktop, false
		headless, and on the web the browser's own answer -- false in an iframe without
		`allowfullscreen`, or under a permissions policy that forbids it.

		Ask this to decide whether to offer the button, rather than learning it from a
		refused `requestFullscreen`. It does not cover the web's other condition, a user
		gesture, which only the request itself can meet.
	**/
	public static inline function hasFullscreen():Bool
		return Raw.wgr_window_has_fullscreen();

	/**
		Hiding it doesn't stop the loop — the program keeps running either way.

		A property and nothing else: wgrender's setter returns `true` on every platform
		it has, so a method form would return a constant and read like a question that
		had been asked.
	**/
	public static inline function isVisible():Bool
		return Raw.wgr_window_is_visible();

	/**
		Hiding it doesn't stop the loop — the program keeps running either way.

		A property and nothing else: wgrender's setter returns `true` on every platform
		it has, so a method form would return a constant and read like a question that
		had been asked.
	**/
	public static inline function setVisible(value:Bool):Bool
		return Raw.wgr_window_set_visible(value);


	public static inline function isFocused():Bool
		return Raw.wgr_window_is_focused();

	/** Monitors the platform reports; 1 on the web. **/
	public static inline function getMonitorCount():Int
		return Raw.wgr_window_get_monitor_count();

	/** Which one the window is on. **/
	public static inline function getMonitor():Int
		return Raw.wgr_window_get_monitor();

	/** Which one the window is on. **/
	public static inline function setMonitor(value:Int):Bool
		return Raw.wgr_window_set_monitor(value);

	public static inline function setSize(width:Int, height:Int):Bool
		return Raw.wgr_window_set_size(width, height);

	/**
		Move it. Only where the platform lets a program place its own windows — not the
		web, and not a Wayland desktop, where the compositor places them.
	**/
	public static inline function setPosition(x:Int, y:Int):Bool
		return Raw.wgr_window_set_position(x, y);

	public static inline function getMonitorSize(monitor:Int):Vec2
		return Vec2.of(Raw.wgr_window_get_monitor_size(monitor));

	public static inline function getMonitorPosition(monitor:Int):Vec2
		return Vec2.of(Raw.wgr_window_get_monitor_position(monitor));

	/** The monitor's name, or "" if the platform doesn't know it. **/
	public static inline function getMonitorName(monitor:Int):String
		return Raw.wgr_window_get_monitor_name(monitor);
}
