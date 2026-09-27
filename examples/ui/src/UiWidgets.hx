// Widgets for the examples — buttons, a progress bar and a scrolling list, built from
// wgrender's public API (2D shapes, text2d, scene interaction) the way a game would.
//
// wgrender has no widget API on purpose (docs/ROADMAP.md, "GUI direction"): what a
// widget is — how it's themed, which one has focus, how it takes the keyboard — is
// policy, and a library that picks it for everyone is the GUI toolkit that decision
// rules out. This file is example code, not API: copy it, change it, throw it away.
// Anything here that wgrender's public API can't express is a gap in wgrender, to fix
// there. It is the Haxe half of `examples/ui_widgets.h`, and it stays beside its one
// caller for the same reason the C header does.
//
// Each widget is scene members, so they're drawn, picked and clipped with everything
// else: create them once, then call the update each frame for their colors and what
// the pointer did. Layers decide what's on top; a label sits above its shape and isn't
// pickable, so the shape under it takes the pointer.
import wgr.*;

/** The colors widgets draw themselves in. **/
class UiTheme {
	/** A button's rectangle. **/
	public var idle:Color;

	public var hover:Color;
	public var pressed:Color;
	public var disabled:Color;

	public var text:Color;
	public var textDisabled:Color;

	/** A bar. **/
	public var track:Color;

	public var fill:Color;
	public var knob:Color;

	/** A list. **/
	public var row:Color;

	public var rowSelected:Color;

	public function new() {
		idle = Color.rgba(70, 80, 105, 255);
		hover = Color.rgba(95, 115, 160, 255);
		pressed = Color.rgba(45, 55, 80, 255);
		disabled = Color.rgba(55, 58, 64, 255);
		text = Color.rgba(235, 238, 245, 255);
		textDisabled = Color.rgba(120, 124, 132, 255);
		track = Color.rgba(150, 175, 230, 255);
		fill = Color.rgba(110, 200, 140, 255);
		knob = Color.rgba(235, 238, 245, 255);
		row = Color.rgba(44, 50, 66, 255);
		rowSelected = Color.rgba(80, 110, 90, 255);
	}
}

class Ui {
	/**
		True while the pointer is over or pressing something (its state covers both the
		frame it started and the frames after).
	**/
	public static inline function isActive(state:ButtonState):Bool
		return state == ButtonState.Pressed || state == ButtonState.Down;
}

/** A rounded rectangle with a label centered on it. **/
class UiButton {
	public final shape:Shape2D;
	public final label:Text2D;
	public final x:Float;
	public final y:Float;
	public final width:Float;
	public final height:Float;

	public function new(scene:Scene, layer:Int, text:String, x:Float, y:Float, width:Float, height:Float,
			textSize:Float) {
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
		shape = new Shape2D();
		shape.setRectangle(width, height, 10);
		shape.setPosition(x, y);
		scene.add(shape, layer);

		// centered on the button, so the label needs no measuring
		label = new Text2D(Handle.NONE);
		label.setText(text);
		label.setFontSize(textSize);
		label.setAlign(Center, Middle);
		label.setPosition(x + width * 0.5, y + height * 0.5);
		label.setPickable(false); // the rectangle under it takes the pointer
		scene.add(label, layer + 1);
	}

	/**
		Color it for what the pointer is doing, and say whether it was clicked this
		frame. A disabled button still blocks the pointer; it just doesn't react.
	**/
	public function update(scene:Scene, theme:UiTheme):Bool {
		final on = shape.isEnabled();
		final held = Ui.isActive(scene.getPress(shape));
		final over = Ui.isActive(scene.getHover(shape));
		shape.setColor(!on ? theme.disabled : held ? theme.pressed : over ? theme.hover : theme.idle);
		label.setColor(on ? theme.text : theme.textDisabled);
		return on && scene.isClicked(shape);
	}

	public var enabled(get, set):Bool;

	inline function get_enabled():Bool
		return shape.isEnabled();

	inline function set_enabled(v:Bool):Bool {
		shape.setEnabled(v);
		return v;
	}

	public inline function setText(text:String):Void
		label.setText(text);

	public function destroy():Void {
		label.destroy();
		shape.destroy();
	}
}

/**
	A progress bar: an outlined track, a rounded fill and a round knob at its end.
	Nothing about it is pickable — it shows a value, it doesn't take one.
**/
class UiBar {
	public final track:Shape2D;
	public final fill:Shape2D;
	public final knob:Shape2D;
	public final x:Float;
	public final y:Float;
	public final width:Float;
	public final height:Float;

	public function new(scene:Scene, layer:Int, x:Float, y:Float, width:Float, height:Float) {
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
		track = new Shape2D();
		track.setRectangle(width, height, height * 0.5);
		track.setPosition(x, y);
		track.setOutline(2);
		track.setPickable(false);
		scene.add(track, layer + 1); // over the fill

		fill = new Shape2D();
		fill.setPickable(false);
		scene.add(fill, layer);

		knob = new Shape2D();
		knob.setCircle(height * 0.34);
		knob.setPickable(false);
		scene.add(knob, layer + 1);
	}

	/**
		Show `value` (0..1): the fill grows from the left and the knob rides its end. At
		0 there's nothing to show, so the fill and knob are hidden.
	**/
	public function setValue(value:Float, theme:UiTheme):Void {
		final clamped = value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
		final round = height * 0.5;
		final length = width * clamped;
		final any = clamped > 0.0;

		track.setColor(theme.track);
		fill.setRectangle(length > height ? length : height, height, round);
		fill.setPosition(x, y);
		fill.setColor(theme.fill);
		fill.setVisible(any);
		knob.setPosition(x + (length > round ? length - round : round), y + round);
		knob.setColor(theme.knob);
		knob.setVisible(any);
	}

	public function destroy():Void {
		knob.destroy();
		fill.destroy();
		track.destroy();
	}
}

/**
	Rows of text in a box: the wheel scrolls them and clicking one selects it. The rows
	and their labels live on two layers clipped to the same box, so a row scrolled out
	of it is neither drawn nor hit.
**/
class UiList {
	public final rows:Array<Shape2D> = [];
	public final labels:Array<Text2D> = [];
	public var selected = -1;

	final x:Float;
	final y:Float;
	final width:Float;
	final height:Float;
	final rowHeight:Float;

	var scroll = 0.0;

	public function new(scene:Scene, rowLayer:Int, names:Array<String>, x:Float, y:Float, width:Float, height:Float,
			rowHeight:Float, textSize:Float) {
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
		this.rowHeight = rowHeight;
		for (name in names) {
			final row = new Shape2D();
			row.setRectangle(width, rowHeight - 4.0, 6);
			scene.add(row, rowLayer);
			rows.push(row);

			final label = new Text2D(Handle.NONE);
			label.setText(name);
			label.setFontSize(textSize);
			label.setAlign(Left, Middle);
			label.setPickable(false);
			scene.add(label, rowLayer + 1);
			labels.push(label);
		}
		scene.setClip(rowLayer, x, y, width, height);
		scene.setClip(rowLayer + 1, x, y, width, height);
		place();
	}

	/** Where a row sits now, given the scroll. **/
	function place():Void {
		for (i in 0...rows.length) {
			final rowY = y + i * rowHeight - scroll;
			rows[i].setPosition(x, rowY);
			labels[i].setPosition(x + 12, rowY + rowHeight * 0.5);
		}
	}

	/**
		Scroll by `wheel` notches, color the rows, and return the selected row (-1 for
		none). Pass `Input.getMouseState().wheel`.
	**/
	public function update(scene:Scene, theme:UiTheme, wheel:Float):Int {
		final span = rows.length * rowHeight - height;
		final most = span > 0.0 ? span : 0.0;

		if (wheel != 0.0) {
			scroll -= wheel * rowHeight;
			scroll = scroll < 0.0 ? 0.0 : scroll > most ? most : scroll;
		}
		place();
		for (i in 0...rows.length) {
			final over = Ui.isActive(scene.getHover(rows[i]));
			if (scene.isClicked(rows[i]))
				selected = i;
			rows[i].setColor(selected == i ? theme.rowSelected : over ? theme.hover : theme.row);
			labels[i].setColor(theme.text);
		}
		return selected;
	}

	public function destroy():Void {
		for (label in labels)
			label.destroy();
		for (row in rows)
			row.destroy();
		rows.resize(0);
		labels.resize(0);
		selected = -1;
	}
}
