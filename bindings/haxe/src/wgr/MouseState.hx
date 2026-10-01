package wgr;

// wgr_input.h

@:structInit
class MouseState {
	public final x:Int;
	public final y:Int;

	/** Scroll this frame: about one unit per wheel notch. **/
	public final wheel:Float;

	public final wheelX:Float;
	public final left:ButtonState;
	public final right:ButtonState;
	public final middle:ButtonState;

	/** Left, right and middle, by index. **/
	public final buttons:Array<ButtonState>;
	public final dx:Int;
	public final dy:Int;

	public function new(x:Int, y:Int, wheel:Float, wheelX:Float, left:ButtonState, right:ButtonState,
			middle:ButtonState, buttons:Array<ButtonState>, dx:Int, dy:Int) {
		this.x = x;
		this.y = y;
		this.wheel = wheel;
		this.wheelX = wheelX;
		this.left = left;
		this.right = right;
		this.middle = middle;
		this.buttons = buttons;
		this.dx = dx;
		this.dy = dy;
	}
}
