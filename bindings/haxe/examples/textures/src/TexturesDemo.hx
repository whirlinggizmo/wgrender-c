// wgrender's textures example, as a Haxe guest: compressed textures against PNG.
//
// A port of examples/textures.c. Each texture twice: from its PNG on the left, and as
// "name.ktx" on the right, for which wgrender picks the file this GPU can use --
// name.bc7.ktx on desktops, name.astc.ktx on phones, name.etc2.ktx on older ones, and
// the PNG if none of them fit. The variants are made beforehand by wgrender's
// tools/compress_textures.sh; asking for ".ktx" is the whole of the API.
//
// Under each: the file that actually loaded, what it costs in GPU memory, and how long
// it took from asking to having it. A texture loads on create; once it's `Ready`,
// `texture.getPath()` says which file it was read from.
//
//   ESC  quit
import wgr.*;

@:expose("WgrGuest")
class TexturesDemo {
	static inline final SCREEN_WIDTH = 1000;
	static inline final SCREEN_HEIGHT = 380;
	static inline final KINDS = 2; // 0: the PNG, 1: the .ktx
	static final NAMES = ["sprites/logo/wg-logo-bw-alpha", "textures/flame"];

	static inline final TILE = 245.0;
	static inline final SIZE = 220.0;

	static var slots:Array<Slot> = [];

	static function main():Void {
		GuestAbi.autostart(start);
	}

	public static function start(host:Dynamic):Bool {
		GuestAbi.attach(host);
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt)); // ensures nothing
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "textures (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		for (t in 0...NAMES.length) {
			for (k in 0...KINDS) {
				final path = '${NAMES[t]}.${k == 0 ? "png" : "ktx"}';
				final slot = new Slot();
				slot.asked = Wgr.getTime();
				slot.texture = new Texture(path); // kept to read its status and size
				slot.sprite = new Sprite2D(slot.texture);
				slot.sprite.setSize(SIZE, SIZE);
				slots.push(slot);
			}
		}
	}

	/**
		GPU memory with the full mipmap chain, which is a third more than the base
		level: 4 bytes a pixel as RGBA, 1 as BC7, ASTC 4x4 or ETC2 RGBA.
	**/
	static function gpuKb(slot:Slot):Float {
		final compressed = slot.loaded.indexOf(".ktx") >= 0;
		return slot.width * slot.height * (compressed ? 1.0 : 4.0) * 4.0 / 3.0 / 1024.0;
	}

	static function basename(path:String):String {
		final cut = path.lastIndexOf("/");
		return cut < 0 ? path : path.substr(cut + 1);
	}

	static function onFrame(dt:Float):Void {
		Render.beginFrame();
		Render.clearBackground(Color.rgba(38, 42, 54, 255));
		Text.draw("wgrender + sokol — textures: PNG (left) and compressed (right)", 12, 36, 22, Color.RAYWHITE);

		for (i in 0...slots.length) {
			final slot = slots[i];
			final x = 20.0 + i * TILE;
			final y = 80.0;
			if (slot.took == 0.0 && slot.texture.getStatus() == Ready) {
				final size = slot.texture.getSize();
				slot.took = Wgr.getTime() - slot.asked;
				slot.width = Std.int(size.x);
				slot.height = Std.int(size.y);
				slot.loaded = slot.texture.getPath();
			} else if (slot.loaded == "" && slot.texture.getStatus() == Failed) {
				slot.loaded = 'failed: ${NAMES[Std.int(i / KINDS)]}';
			}
			slot.sprite.setPosition(x + SIZE * 0.5, y + SIZE * 0.5); // the pivot is the middle
			slot.sprite.draw(); // nothing until it's loaded
			Text.draw(slot.loaded == "" ? "loading..." : basename(slot.loaded), Std.int(x), Std.int(y) + 236, 16,
				Color.LIGHTGRAY);
			if (slot.took > 0.0)
				Text.draw('${slot.width}x${slot.height}  GPU ${Math.round(gpuKb(slot))} KB  '
					+ '${Math.round(slot.took * 1000)} ms', Std.int(x), Std.int(y) + 258, 14, Color.LIGHTGRAY);
		}
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
	}
}

/** One texture and what it cost. **/
private class Slot {
	public var texture:Texture = Handle.NONE;
	public var sprite:Sprite2D = Handle.NONE;
	public var loaded = ""; // the file it was read from, once it's Ready
	public var asked = 0.0;
	public var took = 0.0; // 0 until the texture is Ready
	public var width = 0;
	public var height = 0;

	public function new() {}
}
