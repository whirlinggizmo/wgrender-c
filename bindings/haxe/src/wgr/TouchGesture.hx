package wgr;

// wgr_input.h — two fingers at once

/** Fields in the C struct's order; see `Touch`. **/
@:structInit
class TouchGesture {
	/** Two or more fingers down. **/
	public final active:Bool;

	/** The point between them. **/
	public final x:Float;

	public final y:Float;

	/** Two-finger pan this frame (or tick). **/
	public final dx:Float;

	public final dy:Float;

	/** Pinch this frame: the ratio of their distances, 1 is none. **/
	public final scale:Float;

	/** Twist this frame, radians, clockwise on screen. **/
	public final rotation:Float;

	public function new(active, x, y, dx, dy, scale, rotation) {
		this.active = active;
		this.x = x;
		this.y = y;
		this.dx = dx;
		this.dy = dy;
		this.scale = scale;
		this.rotation = rotation;
	}
}
