package wgr;

// wgr_input.h — one finger

/**
	A touch point. The constructor takes the C struct's fields in order, which is what
	lets `tools/gen_raw.py` generate the read on the JS side.
**/
@:structInit
class Touch {
	/** Stable while the finger is down. **/
	public final id:Int;

	/** Logical pixels, like the mouse. **/
	public final x:Float;

	public final y:Float;

	/** Moved this frame (or tick). **/
	public final dx:Float;

	public final dy:Float;

	/** `Pressed`, `Down` or `Released`. **/
	public final state:ButtonState;

	public function new(id, x, y, dx, dy, state) {
		this.id = id;
		this.x = x;
		this.y = y;
		this.dx = dx;
		this.dy = dy;
		this.state = state;
	}
}
