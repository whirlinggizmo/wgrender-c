// wgrender's ui example, as a Haxe guest: pointer interaction with 2D and 3D members
// of one scene.
//
// The buttons, the progress bar and the scrolling list come from `UiWidgets.hx`, which
// builds them out of 2D shapes, text2d and scene interaction — wgrender has no widget
// API (docs/ROADMAP.md, "GUI direction"), so this is how a game writes one.
//
//   - a nine-slice sprite is the panel behind the controls; presses on it don't orbit
//   - buttons (rounded 2D shapes + centered text2d) react to hover and press;
//     clicking counts and fills the progress bar
//   - "Enable"/"Disable" toggles the third button: disabled, it still blocks the
//     pointer but doesn't react
//   - the note under the bar is wrapped text (`Text2D.maxWidth`)
//   - the list at the bottom is clipped to the panel (`Scene.setClip`): the mouse wheel
//     scrolls it, and rows scrolled out of the box can't be hovered or clicked
//   - the character (a 3D member) lights up on hover; clicking it starts or stops its
//     animation
//   - dragging anywhere else orbits the camera; a drag that starts on a button doesn't
//     (`Input.isPointerCaptured()`)
//   - the header is immediate drawing, next to all that retained UI: the panel's
//     nine-slice texture drawn directly, and a rounded, bordered status pill
//
// Touch works like the mouse. ESC quits.
//
// The C hangs its two per-file callbacks off `wgr_asset_add_task`, one of the calls the
// guest ABI replaces; here the asset op reports each file by the id its load was given.
import UiWidgets;
import wgr.*;

@:expose("WgrGuest")
class UiDemo {
	static inline final SCREEN_WIDTH = 960;
	static inline final SCREEN_HEIGHT = 600;
	static inline final CHARACTER_PATH = "models/woman_casual/woman_casual.glb";
	static inline final PANEL_PATH = "textures/ui_panel.png";
	static inline final CHARACTER_ID = 1;

	// Layers, bottom to top. Each widget puts its labels on the layer above the one
	// it's given (UiWidgets.hx), so a control on CONTROL labels on LABEL, and the
	// list's rows and labels are clipped to the same box on ROW and the one above.
	static inline final LAYER_PANEL = 0;
	static inline final LAYER_CONTROL = 1;
	static inline final LAYER_LABEL = 2;
	static inline final LAYER_ROW = 5;

	static inline final PANEL_X = 10.0;
	static inline final PANEL_Y = 70.0;
	static inline final PANEL_WIDTH = 280.0;
	static inline final PANEL_HEIGHT = 470.0;
	static inline final LIST_X = 30.0;
	static inline final LIST_Y = 400.0;
	static inline final LIST_WIDTH = 240.0;
	static inline final LIST_HEIGHT = 120.0;
	static inline final ROW_HEIGHT = 34.0;

	static final LABELS = ["Count", "Enable / Disable", "Count too"];
	static final ROW_NAMES = [
		"Sponza", "Flight helmet", "Character", "Damaged helmet", "Water bottle", "Lantern", "Sphere grid", "Boom box"
	];

	static var scene:Scene;
	static var camera:Camera3D;
	static var sun:Light;
	static var theme:UiTheme;
	static var background:Color;
	static var highlight:Color;
	static var pill:Color;
	static var pillEdge:Color;
	static var panel:Sprite2D;
	static var divider:Shape2D;
	static var note:Text2D;
	static var buttons:Array<UiButton> = [];
	static var bar:UiBar;
	static var list:UiList;
	static var character:Model;
	static var panelTexture:Texture;
	static var clicks = 0;
	static var animating = false;
	static var yaw = 0.0;

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "ui (wgrender host, Haxe guest)", Msaa4x | Resizable);
	}

	static function placeCamera():Void
		camera.setView(new Vec3(Math.sin(yaw) * 5.0, 1.6, Math.cos(yaw) * 5.0), new Vec3(0, 0.9, 0));

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		theme = new UiTheme();
		background = Color.rgba(30, 34, 44, 255);
		highlight = Color.rgba(255, 220, 120, 255);
		pill = Color.rgba(40, 46, 62, 230);
		pillEdge = Color.rgba(90, 105, 140, 255);

		camera = new Camera3D(Perspective);
		placeCamera();
		scene = new Scene();
		scene.setActiveCamera(camera);
		scene.setAmbient(Color.WHITE, 0.35);
		scene.setInteractive(true);

		sun = new Light(Directional);
		sun.setDirection(new Vec3(-0.4, -1.0, -0.6));
		scene.add(sun, 0);

		character = new Model(Handle.NONE);
		character.setAnimation(3);
		scene.add(character, 0);

		// the panel: one 48x48 texture with 16 px borders, stretched to any size
		panelTexture = new Texture(PANEL_PATH); // kept: the header draws it too
		panel = new Sprite2D(panelTexture);
		panel.setNineSlice(16, 16, 16, 16);
		panel.setPivot(0, 0);
		panel.setPosition(PANEL_X, PANEL_Y);
		panel.setSize(PANEL_WIDTH, PANEL_HEIGHT);
		scene.add(panel, LAYER_PANEL); // pickable, so presses on it don't orbit

		divider = new Shape2D();
		divider.setLine(new Vec2(0, 0), new Vec2(220, 0), 2);
		divider.setPosition(30, 300);
		divider.setColor(theme.disabled);
		divider.setPickable(false);
		scene.add(divider, LAYER_CONTROL);

		bar = new UiBar(scene, LAYER_CONTROL, 30, 320, 220, 18);

		// wrapped note: laid out inside 220 pixels, breaking between words
		note = new Text2D(Handle.NONE);
		note.setText("Every click fills the bar. The list below is clipped to the panel: scroll it with the wheel.");
		note.setFontSize(14);
		note.setMaxWidth(220);
		note.setPosition(30, 352);
		note.setColor(theme.textDisabled);
		note.setPickable(false);
		scene.add(note, LAYER_LABEL);

		for (i in 0...LABELS.length)
			buttons.push(new UiButton(scene, LAYER_CONTROL, LABELS[i], 30, 100.0 + 60.0 * i, 220, 44, 18));
		// the list clips its rows and their labels to its box (LAYER_ROW, LAYER_ROW + 1)
		list = new UiList(scene, LAYER_ROW, ROW_NAMES, LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT, ROW_HEIGHT, 15);

		GuestAbi.loadAsset(CHARACTER_PATH, CHARACTER_ID);
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		if (!ok) {
			Log.error('load failed: $path');
			return;
		}
		switch (id) {
			case CHARACTER_ID:
				final mesh = new Mesh(path);
				character.setMesh(mesh);
				mesh.release();
		}
	}

	static function onFrame(dt:Float):Void {
		final keys = Input.getKeyboardState();
		final mouse = Input.getMouseState();
		final hovered = scene.getHovered();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && keys.isPressed(Escape))
			Wgr.requestQuit();

		// buttons: each colors itself and says whether it was clicked
		final counted = buttons[0].update(scene, theme);
		final toggled = buttons[1].update(scene, theme);
		final countedToo = buttons[2].update(scene, theme);
		if (counted || countedToo)
			clicks++;
		if (toggled)
			buttons[2].enabled = !buttons[2].enabled;
		bar.setValue((clicks % 11) / 10.0, theme);

		// the clipped list: the wheel scrolls it, clicking a row selects it
		final selected = list.update(scene, theme, mouse.wheel);

		// the 3D model
		final hover = scene.getHover(character);
		character.setTint(Ui.isActive(hover) ? highlight : Color.WHITE);
		if (scene.isClicked(character))
			animating = !animating;
		if (animating)
			character.animate(dt);

		// orbit, unless the press started on UI
		if (mouse.left == ButtonState.Down && !Input.isPointerCaptured()) {
			yaw -= mouse.dx * 0.01;
			placeCamera();
		}

		Render.beginFrame();
		Render.clearBackground(background);
		scene.draw();
		// the header is immediate drawing, next to the retained panel below it: the same
		// nine-slice texture as the panel, and a rounded, bordered pill for the status
		panelTexture.drawNineSlice(0, 0, 0, 0, 16, 16, 16, 16, 10, 6, 640, 62);
		Shape2D.drawRoundedRectangle(18, 40, 624, 22, 11, pill);
		Shape2D.drawBorder(18, 40, 624, 22, 1, 1, 1, 1, 11, 11, 11, 11, pillEdge);
		Text.draw("wgrender ui: hover, press and click 2D and 3D members", 22, 15, 20, theme.text);
		final what = hovered.isNone() ? "nothing" : same(hovered, character) ? "the character" : same(hovered,
			panel) ? "the panel" : "UI";
		Text.draw('clicks: $clicks   selected: ${selected < 0 ? "nothing" : ROW_NAMES[selected]}   '
			+ 'hovered: $what   pointer captured: ${Input.isPointerCaptured() ? "yes" : "no"}', 28, 43, 15,
			theme.textDisabled);
		Render.endFrame();
	}

	/** The C compares two handles with `==`; here both sides go through `Handle` first. **/
	static inline function same(a:SceneMember, b:SceneMember):Bool
		return (a : Handle) == (b : Handle);
}
