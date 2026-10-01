// wgrender's textures example, as a Haxe guest: compressed textures against PNG.
//
// A port of examples/textures.c. Each texture twice: from its PNG on the left, and as
// "name.ktx" on the right, for which wgrender picks the file this GPU can use --
// name.bc7.ktx on desktops, name.astc.ktx on phones, name.etc2.ktx on older ones, and
// the PNG if none of them fit. The variants are made beforehand by wgrender's
// tools/compress_textures.sh; asking for ".ktx" is the whole of the API.
//
// Under each: the file that actually loaded, what it costs in GPU memory, and how long
// it took from asking to having it.
//
//   ESC  quit
//
// The C hangs a slot struct off each load's `void *`. The guest ABI has no user
// pointer -- the asset op hands back the id the load was queued with -- so the id *is*
// the slot index here, which is less machinery for the same thing.
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
		GuestAbi.register(onInit, (dt, _) -> onFrame(dt), onAsset);
		return GuestAbi.start(SCREEN_WIDTH, SCREEN_HEIGHT, "textures (wgrender host, Haxe guest)",
			Msaa4x | Resizable);
	}

	static function onInit():Void {
		Asset.setHost(Assets.defaultBase());
		Asset.setManifest(Assets.MANIFEST);
		for (t in 0...NAMES.length) {
			for (k in 0...KINDS) {
				final slot = new Slot();
				slot.asked = Wgr.getTime();
				slots.push(slot);
				// The id is the slot index; ids start at 1 so 0 stays "not an asset".
				GuestAbi.loadAsset('${NAMES[t]}.${k == 0 ? "png" : "ktx"}', slots.length);
			}
		}
	}

	static function onAsset(id:Int, path:String, ok:Bool):Void {
		final slot = slots[id - 1];
		if (slot == null)
			return;
		if (!ok) {
			slot.loaded = 'failed: $path';
			return;
		}
		slot.took = Wgr.getTime() - slot.asked;
		slot.loaded = path;

		final texture = new Texture(path);
		if (texture.isNone())
			return;
		final size = texture.getSize();
		slot.width = Std.int(size.x);
		slot.height = Std.int(size.y);
		slot.sprite = new Sprite2D(texture);
		texture.release(); // the sprite holds its own reference
		slot.sprite.setSize(SIZE, SIZE);
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
			if (!slot.sprite.isNone()) {
				slot.sprite.setPosition(x + SIZE * 0.5, y + SIZE * 0.5); // the pivot is the middle
				slot.sprite.draw();
			}
			Text.draw(slot.loaded == "" ? "loading..." : basename(slot.loaded), Std.int(x), Std.int(y) + 236, 16,
				Color.LIGHTGRAY);
			if (!slot.sprite.isNone())
				Text.draw('${slot.width}x${slot.height}  GPU ${Math.round(gpuKb(slot))} KB  '
					+ '${Math.round(slot.took * 1000)} ms', Std.int(x), Std.int(y) + 258, 14, Color.LIGHTGRAY);
		}
		Render.endFrame();

		// on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to
		if (Wgr.getPlatform() != "web" && Input.isKeyPressed(Escape))
			Wgr.requestQuit();
	}
}

/** One loaded texture and what it cost. **/
private class Slot {
	public var sprite:Sprite2D = Handle.NONE;
	public var loaded = "";
	public var asked = 0.0;
	public var took = 0.0;
	public var width = 0;
	public var height = 0;

	public function new() {}
}
