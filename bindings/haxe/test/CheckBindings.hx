// Runs the binding against a headless wgrender and asserts what it gets back.
//
// It used to only compile, which proves the API exists but not that it is wired to the
// right C call — a grouped wrapper with two arguments transposed compiles perfectly.
// These assertions catch that class: field order in the struct reads, property
// round-trips, packing, and the handful of behaviours that are cheap to state exactly.
//
// `tools/check_binding.py` builds this against wgrender's headless library
// (build/linux-x64-release-headless/, ...: no window, GPU or audio) and runs it for a few frames.
// Non-zero exit means a failure.
import wgr.*;

class CheckBindings {
	static inline final WIDTH = 640;
	static inline final HEIGHT = 480;

	static var checks = 0;
	static var failures = 0;
	static var frames = 0;

	static var scene:Scene;
	static var camera:Camera3D;
	static var model:Model;
	static var confetti:Emitter2D;
	static var label:Text2D;
	static var debugFont:Font;

	/** Deliberately never assigned: a field needs no initialiser (a local does). **/
	static var neverSet:Model;

	/** What the program's own fetcher was asked for; checked once the loop has run. **/
	static var fetchedUrl:String = null;

	/** The assertions are target-neutral so js can type-check them; only `main` isn't. **/
	static function say(line:String):Void {
		#if sys
		Sys.println(line);
		#else
		js.Browser.console.log(line);
		#end
	}

	static function check(ok:Bool, what:String, ?pos:haxe.PosInfos):Void {
		checks++;
		if (!ok) {
			failures++;
			say('FAIL ${pos.fileName}:${pos.lineNumber}  $what');
		}
	}

	static function eq(actual:Dynamic, expected:Dynamic, what:String, ?pos:haxe.PosInfos):Void {
		checks++;
		if (actual != expected) {
			failures++;
			say('FAIL ${pos.fileName}:${pos.lineNumber}  $what: got $actual, expected $expected');
		}
	}

	static function near(actual:Float, expected:Float, what:String, ?pos:haxe.PosInfos):Void {
		checks++;
		if (Math.abs(actual - expected) > 0.001) {
			failures++;
			say('FAIL ${pos.fileName}:${pos.lineNumber}  $what: got $actual, expected ~$expected');
		}
	}

	// --- values and packing -------------------------------------------------

	static function checkValues():Void {
		// a field with no initialiser is none — the thing that is quietly wrong on js
		// if isNone only tests == 0
		check(Model.isNone(neverSet), "an uninitialised handle field is none");
		check(Handle.NONE.isNone, "Handle.NONE is none");

		// 0xRRGGBBAA, which is what every colour constant in wgr_color.h is
		eq((Color.rgba(0x12, 0x34, 0x56, 0x78) : Int), 0x12345678, "Color.rgba packs RRGGBBAA");
		eq((Color.rgba(255, 255, 255, 255) : Int), (Color.WHITE : Int), "rgba(255,255,255,255) is WHITE");
		eq((Color.rgba(0, 0, 0, 255) : Int), (Color.BLACK : Int), "rgba(0,0,0,255) is BLACK");
		eq((Color.rgba(245, 245, 245, 255) : Int), (Color.RAYWHITE : Int), "RAYWHITE");

		// enum values against wgr_types.h / wgr_keys.h
		eq((ButtonState.Up : Int), 0, "ButtonState.Up");
		eq((ButtonState.Pressed : Int), 1, "ButtonState.Pressed");
		eq((ButtonState.Released : Int), 3, "ButtonState.Released");
		eq((SpriteFacing.Free : Int), 3, "SpriteFacing.Free");
		eq((AlphaMode.Add : Int), 3, "AlphaMode.Add is the default mode");
		eq((Key.Escape : Int), 256, "Key.Escape");
		eq((Key.Space : Int), 32, "Key.Space");
		eq((AlignX.Left : Int), 0, "AlignX.Left");
		eq((AlignY.Middle : Int), 4, "AlignY.Middle — the two align enums share one C enum");
		eq((WindowFlag.Msaa4x | WindowFlag.Resizable : Int), 0x24, "window flags or together");

		check(Wgr.getPlatform().length > 0, "getPlatform is not empty");
		// the binding was generated from a particular wgrender; this is the one running
		eq(Version.runtime(), Version.BUILT, "the running library matches what we built against");
		check(Version.check(), "Version.check agrees");
		check(Version.runtimeLabelled().indexOf(Version.BUILT) == 0, "the labelled string starts with it");
		check(Assets.defaultBase().length > 0, "an asset base was resolved");
		check(Wgr.getTime() >= 0, "getTime is sane");
	}

	// --- struct reads: the field order a compile check cannot see -----------

	static function checkStructs():Void {
		final screen = Window.getScreenSize();
		near(screen.x, WIDTH, "screen width — vec2_t field order");
		near(screen.y, HEIGHT, "screen height");

		final mouse = Input.getMouseState();
		eq(mouse.buttons.length, 3, "mouse buttons array");
		check(mouse.left == ButtonState.Up && mouse.buttons[2] == ButtonState.Up, "no button held in headless, as a ButtonState");
		check(mouse.x == 0 && mouse.y == 0, "no pointer in headless, so the mouse is at the origin");

		// nothing in the scene yet, so a pick must miss rather than report garbage
		final miss = Scene.pick(scene, WIDTH / 2, HEIGHT / 2);
		check(!miss.hit, "picking an empty scene misses");
		check(miss.handle.isNone, "a miss carries no handle");

		final wide = Text.measure("iiii", 16);
		final wider = Text.measure("WWWWWWWW", 16);
		check(wide > 0, "built-in text measures wider than nothing");
		check(wider > wide, "eight characters measure wider than four");
	}

	// --- handles, properties and the round-trips that have getters ----------

	static function checkHandles():Void {
		check(!Scene.isNone(scene), "scene created");
		check(!Camera3D.isNone(camera), "camera created");

		final light = Light.create(Point);
		check(!Light.isNone(light), "light created");
		check(Light.isEnabled(light), "a light starts enabled");
		Light.setEnabled(light, false);
		check(!Light.isEnabled(light), "light.enabled round-trips");
		Light.setEnabled(light, true);
		// Directional and spot lights cast; a point light is refused, because it would
		// need six maps (wgr_light.c: "point lights don't cast shadows yet").
		// NB wgr_light.h's comment still says spot is ignored too — it is out of date.
		check(!Light.getCastsShadows(light), "a light starts not casting");
		Light.setCastsShadows(light, true);
		check(!Light.getCastsShadows(light), "a point light refuses to cast: it would need a cube map");

		final sun = Light.create(Directional);
		Light.setCastsShadows(sun, true);
		check(Light.getCastsShadows(sun), "a directional light casts, and it round-trips");
		Light.destroy(sun);

		final torch = Light.create(Spot);
		Light.setSpotCone(torch, 0.3, 0.6);
		Light.setCastsShadows(torch, true);
		check(Light.getCastsShadows(torch), "a spot light casts too — wgri_shadow_fit_spot");
		Light.destroy(torch);
		Light.setColor(light, Color.GOLD);
		Light.setPosition(light, new Vec3(1, 2, 3));
		Light.setRange(light, 10);
		Light.setSpotCone(light, 0.2, 0.4);
		Light.setShadowBias(light, 0.002, 2);
		Light.setShadowDistance(light, 50);
		Light.setShadowMapSize(light, 512);
		Light.setShadowStrength(light, 0.5);
		Light.setShadowColor(light, Color.BLACK);
		Light.destroy(light);

		final material = Material.create(Pbr);
		check(!Material.isNone(material), "material created");
		eq((Material.getShading(material) : Int), (MaterialShading.Pbr : Int), "a new material is Pbr");
		Material.setShading(material, Unlit);
		eq((Material.getShading(material) : Int), (MaterialShading.Unlit : Int), "material.shading round-trips");
		check(!Material.isDoubleSided(material), "a new material is single sided");
		Material.setDoubleSided(material, true);
		check(Material.isDoubleSided(material), "material.doubleSided round-trips");
		check(Material.setFloat(material, "roughness", 0.5), "a known parameter name is accepted");
		check(!Material.setFloat(material, "no_such_parameter", 1), "an unknown parameter name is refused");
		Material.setMetallic(material, 0.5);
		Material.setNormalScale(material, 1);
		Material.setOcclusionStrength(material, 1);
		Material.setBaseColor(material, 1, 1, 1);
		Material.setEmissive(material, 0, 0, 0);
		Material.setVec2(material, "v", 1, 2);
		Material.setInt(material, "i", 1);
		Material.setColor(material, "c", Color.WHITE);
		Material.setAlphaMode(material, Blend);
		check(Material.getShader(material).isNone, "a built-in material has no custom shader");
		material.release();

		// a resource from a path that isn't there must come back as none, not garbage
		// except a texture, which loads on create: a handle at once, failing in a later frame
		final missing = Texture.create("no/such/texture.png");
		check(!Texture.isNone(missing) && missing.getStatus() == Pending, "a missing texture is pending at first");
		missing.release();
		final noMesh = Mesh.create("no/such/mesh.glb"); // loads on create: pending, failing later
		check(!Mesh.isNone(noMesh) && noMesh.getStatus() == Pending, "a missing mesh is pending at first");
		noMesh.release();
		final silence = Audio.create("no/such/sound.mp3");
		check(!Audio.isNone(silence) && silence.getStatus() == Pending, "a missing sound is pending at first");
		silence.release();
	}

	static function checkText():Void {
		label = Text2D.create(Handle.NONE); // no font yet: the default font
		check(!Text2D.isNone(label), "text2d created without a font");
		Text2D.setText(label, "measure me");
		Text2D.setFontSize(label, 20);
		near(Text2D.getFontSize(label), 20, "the font size reads back");
		Text2D.setPosition(label, new Vec2(10, 20));
		Text2D.setColor(label, Color.BLACK);
		Text2D.setMaxWidth(label, 0);
		check(Text2D.isVisible(label), "text2d starts visible");
		Text2D.setVisible(label, false);
		check(!Text2D.isVisible(label), "text2d.visible round-trips");
		Text2D.setVisible(label, true);
		check(Text2D.isPickable(label), "text2d starts pickable");
		Text2D.setPickable(label, false);
		check(!Text2D.isPickable(label), "text2d.pickable round-trips");
		Text2D.setPickable(label, true);
		check(Text2D.isEnabled(label), "text2d starts enabled");
		Text2D.setEnabled(label, false);
		check(!Text2D.isEnabled(label), "text2d.enabled round-trips");
		Text2D.setEnabled(label, true);
		Text2D.setAlign(label, Center, Middle);

		final size = Text2D.measure(label);
		check(size.x > 0 && size.y > 0, "a text2d with text measures larger than nothing");

		final sign = Text3D.create(Handle.NONE);
		check(!Text3D.isNone(sign), "text3d created");
		Text3D.setText(sign, "world");
		Text3D.setFontSize(sign, 1.5);
		Text3D.setColor(sign, Color.BLUE);
		Text3D.setFacing(sign, CameraFixedY);
		Text3D.setMaxWidth(sign, 4);
		Text3D.setAlign(sign, Left, Bottom);
		Text3D.setTransform(sign, new Vec3(0, 2, 0), new Vec3(0, Math.PI, 0));
		check(Text3D.isVisible(sign) && Text3D.isPickable(sign) && Text3D.isEnabled(sign), "text3d starts visible, pickable and enabled");
		Text3D.destroy(sign);
	}

	static function checkEmitters():Void {
		final texture = Texture.create("no/such.png"); // none is fine: we assert on counts
		confetti = Emitter2D.create(texture);
		check(!Emitter2D.isNone(confetti), "emitter2d created");
		Emitter2D.setMax(confetti, 64);
		Emitter2D.setLife(confetti, 5, 5);
		Emitter2D.setVelocity(confetti, new Vec2(0, -10));
		Emitter2D.setGravity(confetti, new Vec2(0, 10));
		Emitter2D.setSize(confetti, 4, 2, 0);
		Emitter2D.setSpin(confetti, -1, 1);
		Emitter2D.setColor(confetti, Color.WHITE, Color.BLACK);
		Emitter2D.setSource(confetti, 0, 0, 8, 8);
		Emitter2D.setFrames(confetti, 2, 2);
		Emitter2D.setAlphaMode(confetti, Blend);
		Emitter2D.setSpawnBox(confetti, 1, 1);
		Emitter2D.setSpawnCircle(confetti, 1);
		Emitter2D.setDrag(confetti, 0.5);
		Emitter2D.setInheritVelocity(confetti, 0.5);
		Emitter2D.setStretch(confetti, 0);
		Emitter2D.setSeed(confetti, 7);
		Emitter2D.addSizeKey(confetti, 0, 1);
		Emitter2D.clearSizeKeys(confetti);
		Emitter2D.addColorKey(confetti, 0, Color.WHITE);
		Emitter2D.clearColorKeys(confetti);
		Emitter2D.addPaletteColor(confetti, Color.RED);
		Emitter2D.clearPalette(confetti);
		check(Emitter2D.isEmitting(confetti), "an emitter starts emitting");
		eq(Emitter2D.getCount(confetti), 0, "no particles before a frame");
		Emitter2D.burst(confetti, 10);
		eq(Emitter2D.getCount(confetti), 10, "burst makes particles immediately");
		Emitter2D.clear(confetti);
		eq(Emitter2D.getCount(confetti), 0, "clear removes them");
		Emitter2D.setEmitting(confetti, false);
		check(!Emitter2D.isEmitting(confetti), "emitter.emitting round-trips");
		Emitter2D.jump(confetti, new Vec2(1, 1));
		Emitter2D.setPosition(confetti, new Vec2(2, 2));
		Emitter2D.setVisible(confetti, true);

		final spray = Emitter3D.create(texture);
		check(!Emitter3D.isNone(spray), "emitter3d created");
		Emitter3D.setMax(spray, 32);
		Emitter3D.setLife(spray, 5, 5);
		Emitter3D.setPosition(spray, new Vec3(0, 0, 0));
		Emitter3D.setSpawnBox(spray, new Vec3(1, 1, 1));
		Emitter3D.setSpawnSphere(spray, 1);
		Emitter3D.setVelocity(spray, new Vec3(0, 1, 0), 0.1, 0.1);
		Emitter3D.setGravity(spray, new Vec3(0, -9.8, 0));
		eq(Emitter3D.getCount(spray), 0, "an emitter with the default rate of 0 makes nothing");
		Emitter3D.setRate(spray, 100);
		Emitter3D.prewarm(spray, 0.1);
		check(Emitter3D.getCount(spray) > 0, "prewarm with a rate runs the emitter forward");
		Emitter3D.destroy(spray);
		Emitter2D.destroy(confetti);
	}

	static function checkShapes():Void {
		final box = Shape3D.create();
		check(!Shape3D.isNone(box), "shape3d created");
		check(Shape3D.setCube(box, new Vec3(1, 2, 3)), "a shape3d takes a cube");
		check(Shape3D.setSphere(box, 1), "and a sphere — the last form set wins");
		check(Shape3D.setRectangle(box, 2, 1), "and a filled rectangle");
		check(Shape3D.setCircle(box, 1), "and a circle outline");
		check(Shape3D.setLine(box, new Vec3(0, 0, 0), new Vec3(1, 1, 1)), "and a line");
		Shape3D.setColor(box, Color.SKYBLUE);
		Shape3D.setTransform(box, new Vec3(1, 2, 3), new Vec3(0, Math.PI, 0), new Vec3(2, 2, 2));
		check(Shape3D.isVisible(box), "a shape3d starts visible");
		Shape3D.setVisible(box, false);
		check(!Shape3D.isVisible(box), "shape3d.visible round-trips");
		Shape3D.setVisible(box, true);
		check(Shape3D.isPickable(box), "a shape3d starts pickable");
		Shape3D.setPickable(box, false);
		check(!Shape3D.isPickable(box), "shape3d.pickable round-trips");
		Shape3D.setPickable(box, true);
		check(Shape3D.isEnabled(box), "a shape3d starts enabled");
		Shape3D.setEnabled(box, false);
		check(!Shape3D.isEnabled(box), "shape3d.enabled round-trips");
		Shape3D.setEnabled(box, true);

		// a strip is built point by point, and reset by starting a new one
		check(Shape3D.setLineStrip(box), "a strip can be started");
		eq(Shape3D.getPointCount(box), 0, "a new strip is empty");
		Shape3D.addPoint(box, new Vec3(0, 0, 0));
		Shape3D.addPoint(box, new Vec3(1, 0, 0));
		Shape3D.addPoint(box, new Vec3(1, 1, 0));
		eq(Shape3D.getPointCount(box), 3, "addPoint appends");
		Shape3D.setLineStrip(box);
		eq(Shape3D.getPointCount(box), 0, "starting again empties it");

		final badge = Shape2D.create();
		check(!Shape2D.isNone(badge), "shape2d created");
		check(Shape2D.setRectangle(badge, 20, 10, 3), "a shape2d takes a rounded rectangle");
		check(Shape2D.setCircle(badge, 5), "and a circle");
		check(Shape2D.setLine(badge, new Vec2(0, 0), new Vec2(10, 0), 2), "and a line with a thickness");
		Shape2D.setColor(badge, Color.GOLD);
		Shape2D.setOutline(badge, 2);
		check(Shape2D.setPivot(badge, new Vec2(0.5, 0.5)), "pivot is a fraction of the size, not pixels");
		Shape2D.setTransform(badge, new Vec2(10, 20), Math.PI / 4, new Vec2(2, 2));
		check(Shape2D.isVisible(badge) && Shape2D.isPickable(badge) && Shape2D.isEnabled(badge), "a shape2d starts visible, pickable and enabled");
		Shape2D.setVisible(badge, false);
		check(!Shape2D.isVisible(badge), "shape2d.visible round-trips");
		Shape2D.setVisible(badge, true);

		check(Scene.add(scene, box), "a shape3d is a scene member");
		check(Scene.add(scene, badge, 1), "a shape2d is a scene member, on a layer");
	}

	static function checkScene():Void {
		final mesh = Mesh.create("no/such.glb");
		model = Model.create(mesh);
		final sprite = Sprite3D.create(Texture.create("no/such.png"));
		final light = Light.create(Directional);
		Light.setDirection(light, new Vec3(0, -1, 0));
		Light.setIntensity(light, 1);

		// SceneMember takes each kind; the compiler enforces which
		check(Scene.add(scene, model), "a model is a scene member");
		check(Scene.add(scene, sprite, 1), "a sprite3d is a scene member, on a layer");
		check(Scene.add(scene, light), "a light is a scene member");
		check(Scene.add(scene, label), "a text2d is a scene member");
		check(Scene.setAmbient(scene, Color.WHITE, 0.25), "ambient set");
		Scene.setActiveCamera(scene, camera);

		Model.setAnimation(model, 0);
		Model.setAnimationSpeed(model, 1);
		Model.setAnimationLoop(model, true);
		Model.setTint(model, Color.RAYWHITE);
		Model.setTransform(model, new Vec3(0, 0, 0), new Vec3(0, 0, 0), new Vec3(1, 1, 1));
		Sprite3D.setFacing(sprite, Free);
		Sprite3D.setTint(sprite, Color.WHITE);
		Sprite3D.setPosition(sprite, new Vec3(0, 1, 0));

		Camera3D.setView(camera, new Vec3(0, 1, 5), new Vec3(0, 0, 0));
	}

	// --- camera, meshes, and the properties with real getters ---------------

	static function checkCamera():Void {
		final c = Camera3D.create(Perspective);
		eq(Camera3D.getProjection(c), Projection.Perspective, "projection reads back as the enum, not an int");
		Camera3D.setProjection(c, Orthographic);
		eq(Camera3D.getProjection(c), Projection.Orthographic, "projection round-trips");

		Camera3D.setFov(c, 1.0);
		near(Camera3D.getFov(c), 1.0, "fov round-trips");
		Camera3D.setOrthoHeight(c, 4);
		near(Camera3D.getOrthoHeight(c), 4, "orthoHeight round-trips");

		check(Camera3D.setActive(c), "setActive succeeded");
		eq((Camera3D.getActive() : Int), (c : Int), "the active camera is the one just set");
		check(!Camera3D.isNone(Camera3D.getDefault()), "there is a default camera");

		// put the scene's camera back: later checks draw through it
		check(Camera3D.setActive(camera), "the check camera is active again");
		Camera3D.destroy(c);
	}

	static function checkMeshes():Void {
		// generated geometry: no file, so these work in headless
		final cube = Mesh.cube(1, 1, 1);
		check(!Mesh.isNone(cube), "a generated cube mesh exists");
		eq(Mesh.getMaterialCount(cube), 1, "a generated mesh has one material slot");
		check(!Material.isNone(Mesh.getMaterial(cube, 0)), "that slot has a material");

		// deduplicated: the same parameters give the same resource
		final again = Mesh.cube(1, 1, 1);
		eq((again : Int), (cube : Int), "the same parameters return the same mesh");
		again.release();

		check(!Mesh.isNone(Mesh.plane(2, 2, 4)), "a generated plane exists");
		check(!Mesh.isNone(Mesh.sphere(1, 8, 16)), "a generated sphere exists");
		check(!Mesh.isNone(Mesh.cylinder(1, 2, 12)), "a generated cylinder exists");
		check(!Mesh.isNone(Mesh.cone(1, 2, 12)), "a generated cone exists");
		check(!Mesh.isNone(Mesh.capsule(0.5, 2, 4, 12)), "a generated capsule exists");
		check(!Mesh.isNone(Mesh.torus(1, 0.25, 8, 16)), "a generated torus exists");
		check(Mesh.isNone(Mesh.cube(0, 1, 1)), "a size of 0 is refused, not silently accepted");

		final m = Model.create(cube);
		check(m.getMesh().getStatus() == Ready, "a model on a generated mesh is ready at once");
		eq(Model.getAnimationCount(m), 0, "a generated mesh brings no animations");
		near(Model.getAnimationDuration(m, 0), 0, "no animation has no duration");

		// the boolean properties all default on, and all round-trip
		check(Model.isVisible(m) && Model.isPickable(m) && Model.isEnabled(m) && Model.castsShadow(m) && Model.receivesShadow(m),
			"a new model is visible, pickable, enabled and shadowed both ways");
		Model.setVisible(m, false);
		Model.setPickable(m, false);
		Model.setEnabled(m, false);
		Model.setCastsShadow(m, false);
		Model.setReceivesShadow(m, false);
		check(!Model.isVisible(m) && !Model.isPickable(m) && !Model.isEnabled(m) && !Model.castsShadow(m) && !Model.receivesShadow(m),
			"every model flag round-trips false");
		Model.setVisible(m, true);

		// the header's promise: a time set before the mesh arrives applies once it does,
		// so it is remembered rather than dropped when there is nothing to pose yet
		Model.setAnimationTime(m, 0.5);
		near(Model.getAnimationTime(m), 0.5, "animationTime is kept with no animation to pose yet");

		// an override changes what this model draws, not what the mesh holds
		final mesh0 = Mesh.getMaterial(cube, 0);
		final custom = Material.create(Unlit);
		check(Model.setMaterial(m, 0, custom), "a material override is set");
		eq((Model.getMaterial(m, 0) : Int), (custom : Int), "the model draws the override");
		eq((Mesh.getMaterial(cube, 0) : Int), (mesh0 : Int), "the mesh's own slot is untouched");
		custom.release();
		Model.destroy(m);
		cube.release();
	}

	static function checkSprite3D():Void {
		final texture = Texture.create("no/such.png");
		final s = Sprite3D.create(texture);

		Sprite3D.setTransform(s, new Vec3(1, 2, 3), new Vec3(0, 0.5, 0), new Vec3(2, 2, 2));
		final p = Sprite3D.getPosition(s);
		near(p.x, 1, "sprite3d position x reads back");
		near(p.y, 2, "sprite3d position y reads back");
		near(p.z, 3, "sprite3d position z reads back — the vec3 is not transposed");
		near(Sprite3D.getRotation(s).y, 0.5, "sprite3d rotation reads back in radians");
		near(Sprite3D.getScale(s).x, 2, "sprite3d scale reads back");

		check(Sprite3D.isVisible(s) && Sprite3D.isPickable(s) && Sprite3D.isEnabled(s), "a new sprite3d is visible, pickable, enabled");
		Sprite3D.setVisible(s, false);
		Sprite3D.setPickable(s, false);
		Sprite3D.setEnabled(s, false);
		check(!Sprite3D.isVisible(s) && !Sprite3D.isPickable(s) && !Sprite3D.isEnabled(s), "every sprite3d flag round-trips false");
		Sprite3D.setVisible(s, true);

		eq(Sprite3D.getAlphaMode(s), AlphaMode.Blend, "a sprite3d blends by default");
		check(Sprite3D.setAlphaMode(s, Mask, 0.5), "alpha mode set to masked");
		eq(Sprite3D.getAlphaMode(s), AlphaMode.Mask, "alpha mode reads back as the enum");

		Sprite3D.setSize(s, 2);
		check(Sprite3D.setExtent(s, 3, 1), "a rectangular extent is accepted");
		check(!Sprite3D.setExtent(s, 0, 1), "a zero extent is refused");
		check(Sprite3D.setSource(s, 0, 0, 16, 16), "a source region is accepted");
		// a fraction of the quad, not pixels: (0.5, 1) is the bottom edge
		check(Sprite3D.setPivot(s, 0.5, 1), "a pivot is a fraction of the quad");
		check(Sprite3D.setPickAlphaTest(s, true, 0.5), "pick alpha test enabled");

		check(Material.isNone(Sprite3D.getMaterial(s)), "a new sprite3d is on the built-in shader");
		final custom = Material.create(Unlit);
		check(Sprite3D.setMaterial(s, custom), "a sprite3d takes a material");
		eq((Sprite3D.getMaterial(s) : Int), (custom : Int), "and reads it back");
		custom.release();

		check(Sprite3D.setTexture(s, Texture.getDefault()), "a sprite3d's texture can be swapped");
		Sprite3D.destroy(s);
		texture.release();
	}

	/**
		Every kind with a transform: the combined setter, a part set alone leaving the
		others as they were, and each part read back, through the binding's marshalling.
	**/
	static function checkTransforms():Void {
		function vec3(v:Vec3, x:Float, y:Float, z:Float, what:String) {
			near(v.x, x, '$what x');
			near(v.y, y, '$what y');
			near(v.z, z, '$what z');
		}
		function vec2(v:Vec2, x:Float, y:Float, what:String) {
			near(v.x, x, '$what x');
			near(v.y, y, '$what y');
		}

		final model = Model.create(Handle.NONE);
		check(Model.setTransform(model, new Vec3(1, 2, 3), new Vec3(0.1, 0.2, 0.3), new Vec3(4, 5, 6)), "model setTransform");
		check(Model.setPosition(model, new Vec3(7, 8, 9)), "model setPosition");
		vec3(Model.getPosition(model), 7, 8, 9, "model position");
		vec3(Model.getRotation(model), 0.1, 0.2, 0.3, "model rotation survives setPosition");
		check(Model.setScale(model, new Vec3(2, 2, 2)) && Model.setRotation(model, new Vec3(0, 1, 0)), "model setScale, setRotation");
		vec3(Model.getScale(model), 2, 2, 2, "model scale");
		vec3(Model.getRotation(model), 0, 1, 0, "model rotation");

		final sprite = Sprite3D.create(Handle.NONE);
		check(Sprite3D.setTransform(sprite, new Vec3(1, 2, 3), Vec3.ZERO, Vec3.ONE), "sprite3d setTransform");
		check(Sprite3D.setRotation(sprite, new Vec3(0, 0.5, 0)) && Sprite3D.setScale(sprite, new Vec3(3, 3, 3)), "sprite3d setRotation, setScale");
		vec3(Sprite3D.getPosition(sprite), 1, 2, 3, "sprite3d position survives the parts");

		final shape = Shape3D.create();
		check(Shape3D.setPosition(shape, new Vec3(4, 5, 6)) && Shape3D.setScale(shape, new Vec3(0.5, 0.5, 0.5)), "shape3d setPosition, setScale");
		vec3(Shape3D.getPosition(shape), 4, 5, 6, "shape3d position");
		vec3(Shape3D.getScale(shape), 0.5, 0.5, 0.5, "shape3d scale");

		final label = Text3D.create(Handle.NONE);
		check(Text3D.setTransform(label, new Vec3(1, 1, 1), new Vec3(0, 0.25, 0)), "text3d setTransform");
		check(Text3D.setPosition(label, new Vec3(2, 3, 4)), "text3d setPosition");
		vec3(Text3D.getRotation(label), 0, 0.25, 0, "text3d rotation survives setPosition");

		final sprite2D = Sprite2D.create(Handle.NONE);
		check(Sprite2D.setTransform(sprite2D, new Vec2(10, 20), 0.5, new Vec2(2, 3)), "sprite2d setTransform");
		check(Sprite2D.setPosition(sprite2D, new Vec2(30, 40)), "sprite2d setPosition");
		vec2(Sprite2D.getPosition(sprite2D), 30, 40, "sprite2d position");
		near(Sprite2D.getRotation(sprite2D), 0.5, "sprite2d rotation survives setPosition");
		vec2(Sprite2D.getScale(sprite2D), 2, 3, "sprite2d scale");

		final shape2D = Shape2D.create();
		check(Shape2D.setTransform(shape2D, new Vec2(5, 6), 0, Vec2.ONE) && Shape2D.setRotation(shape2D, 1.25), "shape2d setTransform, setRotation");
		vec2(Shape2D.getPosition(shape2D), 5, 6, "shape2d position survives setRotation");
		near(Shape2D.getRotation(shape2D), 1.25, "shape2d rotation");

		final text2D = Text2D.create(Handle.NONE);
		Text2D.setPosition(text2D, new Vec2(12, 34));
		vec2(Text2D.getPosition(text2D), 12, 34, "text2d position");

		Model.destroy(model);
		Sprite3D.destroy(sprite);
		Shape3D.destroy(shape);
		Text3D.destroy(label);
		Sprite2D.destroy(sprite2D);
		Shape2D.destroy(shape2D);
		Text2D.destroy(text2D);
	}

	static function checkSceneState():Void {
		final s = Scene.create();
		final m = Model.create(Mesh.cube(1, 1, 1));

		check(Scene.isCulling(s), "a new scene culls");
		Scene.setCulling(s, false);
		check(!Scene.isCulling(s), "culling round-trips");
		Scene.setCulling(s, true);

		check(!Scene.isInteractive(s), "a new scene is not interactive");
		Scene.setInteractive(s, true);
		check(Scene.isInteractive(s), "interactive round-trips");

		check(Scene.add(s, m, 2), "a member goes onto a layer");
		check(Scene.setLayer(s, m, 3), "and moves to another");
		check(Scene.setClip(s, 3, 0, 0, 100, 100), "a layer clips to a rectangle");
		check(Scene.setClip(s, 3, 0, 0, 0, 0), "a zero rectangle removes the clip");

		// nothing is under a pointer that never moved
		check(SceneMember.isNone(Scene.getHovered(s)), "nothing is hovered in headless");
		eq(Scene.getHover(s, m), ButtonState.Up, "hover is up");
		eq(Scene.getPress(s, m), ButtonState.Up, "press is up");
		check(!Scene.isClicked(s, m), "nothing is clicked");

		check(Scene.setTonemap(s, Aces, 1), "a tonemap and exposure are set");
		final missing = Environment.create("no/such.hdr"); // loads on create: pending, failing later
		check(!Environment.isNone(missing) && missing.getStatus() == Pending, "a missing environment is pending at first");
		missing.release();
		check(Scene.setEnvironment(s, Handle.NONE), "a none environment removes it");
		check(Scene.setBackground(s, Handle.NONE), "a none background removes it");

		check(Scene.remove(s, m), "a member comes out");
		check(!Scene.remove(s, m), "and cannot come out twice");
		Scene.clear(s);
		Model.destroy(m);
		Scene.destroy(s);
	}

	static function checkTexture():Void {
		check(!Texture.isNone(Texture.getDefault()), "there is a default texture");
		check(!Texture.isNone(Texture.getPlaceholder()), "there is a placeholder texture");

		final target = Texture.createTarget(64, 32);
		check(!Texture.isNone(target), "a render target is created");
		final size = Texture.getSize(target);
		near(size.x, 64, "the target's width reads back");
		near(size.y, 32, "the target's height reads back — the vec2 is not transposed");
		target.release();
	}

	// --- values with arithmetic behind them ---------------------------------

	static function checkColor():Void {
		final c = Color.rgba(10, 20, 30, 40);
		eq(Color.getRed(c), 10, "red comes back out");
		eq(Color.getGreen(c), 20, "green comes back out");
		eq(Color.getBlue(c), 30, "blue comes back out — the channels are not rotated");
		eq(Color.getAlpha(c), 40, "alpha comes back out");
		eq((c : Int), 0x0A141E28, "and the packing is 0xRRGGBBAA");

		// out of range saturates; it must not wrap into the neighbouring channel
		final hot = Color.rgba(300, -5, 128, 255);
		eq(Color.getRed(hot), 255, "an over-range component saturates");
		eq(Color.getGreen(hot), 0, "an under-range one saturates the other way");
		eq(Color.getBlue(hot), 128, "and the neighbour is untouched");

		eq((Color.rgbaf(1, 0, 0, 1) : Int), (Color.rgba(255, 0, 0, 255) : Int),
			"the float constructor agrees with the integer one");

		eq((Color.withAlpha(c, 255) : Int), (Color.rgba(10, 20, 30, 255) : Int), "withAlpha changes only alpha");

		final black:Color = Color.rgba(0, 0, 0, 255);
		final white:Color = Color.rgba(255, 255, 255, 255);
		eq((Color.lerp(black, white, 0) : Int), (black : Int), "lerp at 0 is the start");
		eq((Color.lerp(white, black, 0) : Int), (white : Int), "and reads the arguments in that order");
		eq((Color.lerp(black, white, 1) : Int), (white : Int), "lerp at 1 is the end");
		eq(Color.getRed(Color.lerp(black, white, 0.5)), 128, "and halfway rounds up, not down");
		eq((Color.lerp(black, white, 5) : Int), (white : Int), "t is clamped, not extrapolated");
	}

	static function checkHandleKind():Void {
		// a handle knows what it is, which is what a pick result needs
		eq(SceneMember.isNone(Scene.getHovered(scene)), true, "the none handle is none");
		eq(Handle.getKind((Handle.NONE : Handle)), HandleKind.None, "and its kind is None");

		final m = Model.create(Mesh.cube(1, 1, 1));
		eq(Handle.getKind(((m : Handle))), HandleKind.Model, "a model handle knows it is a model");
		final t = Texture.getDefault();
		eq(Handle.getKind(((t : Handle))), HandleKind.Texture, "a texture handle knows it is a texture");
		eq(Handle.getKind(((camera : Handle))), HandleKind.Camera3D, "a camera handle knows it is a camera");
		eq(Handle.getKind(((scene : Handle))), HandleKind.Scene, "a scene handle knows it is a scene");
		Model.destroy(m);
	}

	static function checkSprite2D():Void {
		final s = Sprite2D.create(Texture.getDefault());
		check(!Sprite2D.isNone(s), "a sprite2d is created");
		eq(Handle.getKind(((s : Handle))), HandleKind.Sprite2D, "and its handle says so");

		Sprite2D.setPosition(s, new Vec2(10, 20));
		Sprite2D.setRotation(s, 0.5);
		Sprite2D.setScale(s, new Vec2(2, 3));
		Sprite2D.setTint(s, Color.GOLD);

		check(Sprite2D.isVisible(s) && Sprite2D.isPickable(s) && Sprite2D.isEnabled(s), "a new sprite2d is visible, pickable, enabled");
		Sprite2D.setVisible(s, false);
		Sprite2D.setPickable(s, false);
		Sprite2D.setEnabled(s, false);
		check(!Sprite2D.isVisible(s) && !Sprite2D.isPickable(s) && !Sprite2D.isEnabled(s), "every sprite2d flag round-trips false");
		Sprite2D.setVisible(s, true);

		eq(Sprite2D.getAlphaMode(s), AlphaMode.Blend, "a sprite2d blends by default");
		check(Sprite2D.setAlphaMode(s, Add), "alpha mode set to additive");
		eq(Sprite2D.getAlphaMode(s), AlphaMode.Add, "and reads back as the enum");

		check(Sprite2D.setSize(s, 64, 32), "an on-screen size is accepted");
		check(Sprite2D.setSource(s, 0, 0, 16, 16), "a source region is accepted");
		check(Sprite2D.setPivot(s, 0, 0), "a pivot is a fraction of the sprite");
		check(Sprite2D.setNineSlice(s, 4, 4, 4, 4), "a nine-slice is accepted");
		check(Sprite2D.setNineSlice(s, 0, 0, 0, 0), "and all zero turns it off");
		check(Sprite2D.setPickAlphaTest(s, true, 0.25), "pick alpha test enabled");

		check(Material.isNone(Sprite2D.getMaterial(s)), "a new sprite2d is on the built-in shader");
		check(Scene.add(scene, s, 5), "a sprite2d is a scene member");
		check(Scene.remove(scene, s), "and comes back out");
		Sprite2D.destroy(s);
	}

	static function checkInput():Void {
		// headless reports no devices, so these assert shape rather than values
		final p = Input.getMousePosition();
		check(p.x == p.x && p.y == p.y, "a mouse position is two real numbers");
		final d = Input.getMouseDelta();
		near(d.x, 0, "nothing moved the mouse");
		near(d.y, 0, "on either axis");
		near(Input.getMouseWheel().y, 0, "nor the wheel");
		near(Input.getMouseWheel().x, 0, "nor sideways");

		eq(Input.getMouseButton(Left), ButtonState.Up, "no mouse button is down");
		check(!Input.isMouseButtonPressed(Left), "none was pressed");
		check(!Input.isMouseButtonDown(Right), "none is held");
		check(!Input.isMouseButtonReleased(Middle), "none was released");

		eq(Input.getTouchCount(), 0, "nothing is touching the screen");

		check(!Input.isGamepadConnected(0), "no gamepad in headless");
		eq(Input.getGamepadName(0), "", "so slot 0 has no name");
		eq(Input.getGamepadButton(0, South), ButtonState.Up, "and no button is down");
		near(Input.getGamepadAxis(0, LeftX), 0, "and no axis is off centre");
		check(Input.setGamepadDeadzone(0.2), "a deadzone in range is accepted");

		check(!Input.isPointerCaptured(), "nothing has captured the pointer");
		Input.setPointerCaptured(true);
		check(Input.isPointerCaptured(), "a UI can claim it");
		Input.setPointerCaptured(false);
		check(!Input.isKeyboardCaptured(), "nothing has captured the keyboard");
		Input.setKeyboardCaptured(true);
		check(Input.isKeyboardCaptured(), "a UI can claim that too");
		Input.setKeyboardCaptured(false);
	}

	static function checkWindowAndRuntime():Void {
		check(Wgr.isInitialized(), "the runtime says it is initialised");
		eq(Wgr.getRenderer(), "headless", "and names the backend it chose");
		// has_threads is a fact about the build, not something to assert a value for
		check(Wgr.hasThreads() || !Wgr.hasThreads(), "hasThreads answers without throwing");

		final size = Window.getScreenSize();
		check(size.x > 0 && size.y > 0, "the window has a size");
		near(Window.getScreenSize().x, size.x, "the property and the function agree");
		check(!Window.closeRequested(), "nobody has asked to close it");
		check(Window.getMonitorCount() >= 0, "monitors can be counted");

		Window.setTitle("check");
		Window.setVisible(Window.isVisible()); // whatever it is, setting it back is legal

		eq(Version.label(), "dev", "this build is labelled dev");
		check(Version.number() > 0, "and packs a version number");
	}

	static function checkSoundAndAsset():Void {
		final audio = Audio.create("no/such.wav");
		final sound = Sound.create(audio);
		check(Sound.getAudio(sound) == audio, "a sound gives back its audio");
		check(!Sound.isPlaying(sound), "a new sound is not playing");
		Sound.setVolume(sound, 0.5);
		Sound.setPitch(sound, 1.5);
		Sound.setPan(sound, -1);
		Sound.setLoop(sound, true);
		check(!Sound.isPlaying(sound), "setting properties does not start it");
		Sound.stop(sound);
		Sound.pause(sound);
		Sound.resume(sound);
		Sound.destroy(sound);
		audio.release();

		// the host round-trips, and putting it back leaves the later checks alone
		final was = Asset.getHost();
		Asset.setHost("examples/assets");
		eq(Asset.getHost(), "examples/assets", "the asset host round-trips");
		Asset.setHost(was);

		check(Asset.addRedirect("textures/", "mods/hd/textures/"), "a path redirect is added");
		Asset.clearRedirects();
		check(!Asset.addRedirect("", "somewhere/"), "an empty prefix is refused");
		Asset.clearRedirects();

		Asset.setUploadBudget(8);
		check(Asset.setCacheDir(".wgr-cache"), "a cache directory is accepted");
		#if (sys && !emscripten)
		eq(Asset.getCacheDir(), ".wgr-cache", "and is the one downloads go in");
		#end

		// who the program is: nothing unset looks like anyone's
		Wgr.setAppCompany(null);
		eq(Wgr.getAppCompany(), "DefaultCompany", "the company defaults to DefaultCompany");
		Wgr.setAppCompany("Acme/Games");
		eq(Wgr.getAppCompany(), "Acme_Games", "a company is one safe path component");
		Wgr.setAppName("Rocket");
		eq(Wgr.getAppName(), "Rocket", "the app name round-trips");
		Wgr.setAppName(null);
		check(Wgr.getAppName() != "" && Wgr.getAppName() != "Rocket", "and null is the default again");
		Wgr.setAppCompany(null);

		eq(Asset.getCacheMode(), AssetCacheMode.Revalidate, "the cache revalidates by default");
		check(Asset.setCacheMode(AssetCacheMode.Trust), "a cache mode is accepted");
		eq(Asset.getCacheMode(), AssetCacheMode.Trust, "the cache mode round-trips");
		check(Asset.setCacheMode(AssetCacheMode.Revalidate), "the default mode is accepted back");
		check(Asset.setManifest(Assets.MANIFEST), "a relative manifest path is accepted");
		check(!Asset.setManifest("/manifest.json"), "an absolute manifest path is refused");
		check(!Asset.setManifest("https://example.invalid/manifest.json"), "a URL manifest path is refused");
		check(Asset.setManifest(null), "no manifest is accepted");

		final group = Asset.createGroup();
		check(!AssetTask.isNone(group), "an asset group is created");
		eq(Handle.getKind(((group : Handle))), HandleKind.AssetTask, "and it is a task handle");
		check(!Asset.groupAdd(group, group), "a group cannot contain itself");
		near(Asset.getProgress(group), 0, "an empty group has made no progress");
	}

	/**
		The light getters (wgrender 73356f7). Every one of these clamps was unobservable
		from outside wgrender until they landed: the setter returned a bool that said
		"accepted" without saying what it accepted.
	**/
	static function checkLightGetters():Void {
		final l = Light.create(Spot);
		eq(Light.getType(l), LightKind.Spot, "a light knows what kind it was made as");

		// what is held, not what was passed: a direction comes back normalized
		Light.setDirection(l, new Vec3(0, -4, 0));
		near(Light.getDirection(l).y, -1, "direction is held normalized");
		near(Light.getDirection(l).x, 0, "on the other axes too");

		Light.setPosition(l, new Vec3(1, 2, 3));
		near(Light.getPosition(l).x, 1, "position x reads back");
		near(Light.getPosition(l).y, 2, "position y reads back");
		near(Light.getPosition(l).z, 3, "position z reads back — the vec3 is not transposed");

		Light.setColor(l, Color.GOLD);
		eq((Light.getColor(l) : Int), (Color.GOLD : Int), "color reads back");
		Light.setIntensity(l, 2.5);
		near(Light.getIntensity(l), 2.5, "intensity reads back");
		Light.setIntensity(l, -5);
		near(Light.getIntensity(l), 0, "a negative intensity is held as 0");
		Light.setRange(l, -5);
		near(Light.getRange(l), 0, "and so is a negative range");
		Light.setRange(l, 10);
		near(Light.getRange(l), 10, "a real range reads back");

		// the cone clamps to 0..pi/2
		check(Light.setSpotCone(l, 0.1, 3), "a cone past pi/2 is accepted");
		near(Light.getSpotOuterAngle(l), Math.PI / 2, "and clamped to pi/2");
		near(Light.getSpotInnerAngle(l), 0.1, "the inner angle is left alone");

		// the shadow map: clamped to 256..4096 and floored to a power of two
		check(Light.getShadowMapSize(l) == 2048, "the default map size is 2048");
		Light.setShadowMapSize(l, 64);
		eq(Light.getShadowMapSize(l), 256, "64 clamps up to 256");
		Light.setShadowMapSize(l, 9000);
		eq(Light.getShadowMapSize(l), 4096, "9000 clamps down to 4096");
		Light.setShadowMapSize(l, 1500);
		eq(Light.getShadowMapSize(l), 1024, "1500 floors to 1024, not 2048");
		Light.setShadowMapSize(l, 0);
		eq(Light.getShadowMapSize(l), 1024, "0 is refused, so it keeps what it had");

		// strength clamps now, where it used to refuse
		Light.setShadowStrength(l, 1.5);
		near(Light.getShadowStrength(l), 1, "a strength past 1 clamps");
		Light.setShadowStrength(l, -1);
		near(Light.getShadowStrength(l), 0, "and below 0 clamps the other way");
		Light.setShadowStrength(l, 0.25);
		near(Light.getShadowStrength(l), 0.25, "one in range is kept");

		Light.setShadowDistance(l, 20);
		near(Light.getShadowDistance(l), 20, "shadow distance reads back");
		Light.setShadowDistance(l, -1);
		near(Light.getShadowDistance(l), 20, "and a negative one is refused, keeping the last");

		Light.setShadowColor(l, Color.SKYBLUE);
		eq((Light.getShadowColor(l) : Int), (Color.SKYBLUE : Int), "shadow color reads back");

		check(Light.setShadowBias(l, 2, 6), "a bias is set");
		near(Light.getShadowBiasConstant(l), 2, "the constant part reads back");
		near(Light.getShadowBiasSlope(l), 6, "and the slope part — the pair is not swapped");

		// a dead handle reads 0 rather than the last value
		Light.destroy(l);
		near(Light.getIntensity(l), 0, "a destroyed light reads 0, not stale state");

		final none:Light = Handle.NONE;
		near(Light.getRange(none), 0, "and so does a none handle");
	}

	/** What the header sweep of 2026-09-21 says wgrender promises. **/
	static function checkSweptBehaviour():Void {
		// emitters: max is 1..65536 and a value outside is refused, not clamped
		final e = Emitter3D.create(Texture.getDefault());
		check(Emitter3D.setMax(e, 65536), "the largest allowed max is accepted");
		check(!Emitter3D.setMax(e, 65537), "one past it is refused, not clamped");
		check(!Emitter3D.setMax(e, 0), "and so is zero");
		Emitter3D.destroy(e);

		final e2 = Emitter2D.create(Texture.getDefault());
		check(Emitter2D.setMax(e2, 65536), "2D shares the same ceiling");
		check(!Emitter2D.setMax(e2, 65537), "and the same refusal");
		Emitter2D.destroy(e2);

		// a null fetch_url is not an empty one: null keeps redirects and variants,
		// a URL tells wgrender the caller chose that exact file. The js binding sends
		// null as a null pointer for this reason; hxcpp always did.
		final plain = Asset.ensureAsync("no/such.png");
		check(!AssetTask.isNone(plain), "ensureAsync with no fetch url makes a task");
		final sourced = Asset.ensureAsync("no/such2.png", "https://example.invalid/no/such2.png");
		check(!AssetTask.isNone(sourced), "and so does one with a source of its own");
		#if (sys && !emscripten)
		// natively a source is http(s), or a path under a local host: nothing a program
		// is handed can name a local file outside it (the web leaves that to the browser)
		check(AssetTask.isNone(Asset.ensureAsync("no/such3.png", "file:///etc/passwd")),
			"a file: URL source is refused");
		check(AssetTask.isNone(Asset.ensureAsync("no/such3.png", "../outside.png")),
			"and so is one climbing out of a local host");
		#end

		// redirects: a URL target is legal on desktop now, not just on the web
		check(Asset.addRedirect("models/", "https://cdn.example.invalid/models/"),
			"a download redirect is accepted on desktop");
		Asset.clearRedirects();

		#if cpp
		// A fetcher the program installs is the one a URL reaches, whichever order the
		// calls come in: under -D WGR_INCLUDE_FETCHER a URL host set after it used to
		// swap in httpFetcher. The task has a source of its own and the host is a
		// directory, so only the fetchUrl route leads to it. It answers at once, so
		// the task fails on its first tick (callbacks are what arm it); a key at the
		// root makes no directories.
		check(Asset.setFetcher((request, url, _) -> {
			fetchedUrl = url;
			Asset.fetchDone(request, false);
		}), "a fetcher installs on desktop");
		final was = Asset.getHost();
		Asset.setHost("https://example.invalid/assets");
		Asset.setHost(was);
		AssetTask.then(Asset.ensureAsync("no-such3.png", "https://example.invalid/no-such3.png"),
			_ -> {}, _ -> {});
		#end
	}

	static function checkEvents():Void {
		eq(Event.listenerCount("check/ping"), 0, "nothing is listening yet");

		// listening needs a C function pointer, so the bus is hxcpp only; offAll and
		// listenerCount above work on both, which is why they sit outside the guard
		#if cpp

		var heard = 0;
		final token = Event.on("check/ping", () -> heard++);
		check(!EventListener.isNone(token), "on returns a token");
		eq(Event.listenerCount("check/ping"), 1, "and the listener is registered");
		eq(Event.emit("check/ping"), 1, "emitting reaches it");
		eq(heard, 1, "and the Haxe closure ran");

		// once goes away by itself; the plain listener does not
		Event.once("check/ping", () -> heard++);
		eq(Event.listenerCount("check/ping"), 2, "a once listener is registered too");
		Event.emit("check/ping");
		eq(heard, 3, "both ran");
		eq(Event.listenerCount("check/ping"), 1, "and the once listener dropped itself");

		check(Event.off(token), "off takes the token back");
		eq(Event.listenerCount("check/ping"), 0, "and the listener is gone");
		check(!Event.off(token), "a token cannot be used twice");
		Event.emit("check/ping");
		eq(heard, 3, "nothing ran after that");

		Event.on("check/other", () -> heard++);
		eq(Event.offAll("check/other"), 1, "offAll drops what is there");
		eq(Event.listenerCount("check/other"), 0, "leaving nothing");
		#end
	}

	static function checkPick():Void {
		Pick.resetStats();
		final before = Pick.getStats();
		eq(before.broadphaseTests, 0, "stats reset to zero");
		eq(before.narrowphaseHits, 0, "on both phases");

		// nothing is on screen in headless, so this misses rather than hits
		final m = Model.create(Mesh.cube(1, 1, 1));
		final r = Pick.object(m, 10, 10);
		check(!r.hit || r.hit, "picking one object answers without throwing");
		Model.destroy(m);
	}

	// --- lifecycle ----------------------------------------------------------

	// The sugar: each kind is @:using itself, so its statics are methods too, and a
	// plain create has a constructor. Both are the same inline calls as the statics
	// above; these check they reach them, not that the calls work a second time.
	static function checkSugar():Void {
		final m = new Model(Handle.NONE);
		check(!m.isNone(), "new Model(...) makes a model");
		eq((m : Handle).getKind(), HandleKind.Model, "and it is a model handle");
		m.setPosition(new Vec3(1, 2, 3));
		near(Model.getPosition(m).y, 2, "a method call is the static's call");
		near(m.getPosition().z, 3, "and a method reads back what a static would");
		m.setPosition(4, 5, 6);
		near(m.getPosition().x, 4, "the split overload is the same setter");
		m.setScale(2, 2, 2);
		near(m.getScale().y, 2, "and so is setScale's");

		final sprite = new Sprite2D(Handle.NONE);
		check(sprite.setPivot(new Vec2(0.25, 0.75)), "a sprite's pivot takes a Vec2 too");
		near(sprite.getPivot().y, 0.75, "and reads back");
		check(sprite.setPivot(0.5, 1.0), "and the components, as it did");
		near(sprite.getPivot().x, 0.5, "which read back the same way");
		sprite.destroy();
		scene.add(m);
		m.destroy();

		final l = new Light(Directional);
		l.setIntensity(2.5);
		near(Light.getIntensity(l), 2.5, "new Light(...) and a method on it");
		l.destroy();

		final s = new Scene();
		check(!s.isNone(), "new Scene() needs no arguments");
		s.destroy();

		final text = new Text2D(Handle.NONE);
		text.setText("iiii");
		check(text.measure().x > 0 && text.measure().y > 0, "measure reads the laid-out text as a Vec2");
		text.destroy();
	}

	static function onInit():Void {
		Log.setLevel(Fatal); // the missing-asset checks below log errors on purpose
		camera = Camera3D.create(Perspective);
		scene = Scene.create();
		Scene.setActiveCamera(scene, camera);

		checkValues();
		checkStructs();
		checkHandles();
		checkText();
		checkEmitters();
		checkScene();
		checkShapes();
		checkCamera();
		checkTexture();
		checkMeshes();
		checkSprite3D();
		checkTransforms();
		checkSceneState();
		checkColor();
		checkHandleKind();
		checkSprite2D();
		checkInput();
		checkWindowAndRuntime();
		checkSoundAndAsset();
		checkEvents();
		checkPick();
		checkSugar();
		checkSweptBehaviour();
		checkLightGetters();
	}

	static function onFrame(dt:Float, tickFraction:Float):Void {
		frames++;
		check(dt >= 0, "dt is not negative");
		if (frames == 1) {
			// these only mean anything once the loop is running
			check(Wgr.getTime() >= 0, "getTime inside a frame");
			eq(Input.getKey(Escape), ButtonState.Up, "no key is down in headless");
			check(!Input.isKeyPressed(Space), "no key was pressed");

			// the keyboard as a whole. On js this is a heap view, which is why it is
			// read inside a frame and not held: the op edge resets the stack it sits on.
			final keyboard = Input.getKeyboardState();
			eq(keyboard[Key.Escape], ButtonState.Up, "the whole keyboard agrees: escape is up");
			eq(keyboard[Key.Space], ButtonState.Up, "and space is up");
			check(!keyboard.isDown(Key.A) && !keyboard.isPressed(Key.A) && !keyboard.isReleased(Key.A),
				"a key nothing touched is in no state at all");
			eq(keyboard.numPressedKeys, 0, "no key arrived this frame");
			eq(keyboard.numPressedChars, 0, "no character arrived this frame");
			eq(keyboard.pressedChar, 0, "so there is no last character");
			eq(keyboard.typedText(), "", "and no text to append");
		}
		Render.beginFrame();
		Render.clearBackground(Color.RAYWHITE);
		Render.beginMode3D();
		Shape3D.drawGrid(4, 1, Color.DARKGRAY);
		Shape3D.drawCube(new Vec3(0, 0, 0), new Vec3(1, 1, 1), Color.SKYBLUE);
		Shape3D.drawCubeWires(new Vec3(0, 0, 0), new Vec3(1, 1, 1), Color.WHITE);
		Shape3D.drawSphere(new Vec3(0, 0, 0), 1, Color.GOLD);
		Shape3D.drawLine(new Vec3(0, 0, 0), new Vec3(1, 1, 1), Color.LIME);
		Shape3D.drawRectangle(new Vec3(0, 0, 0), 1, 1, Vec3.ZERO, Color.RED);
		Shape3D.drawCircle(new Vec3(0, 0, 0), 1, Vec3.ZERO, Color.VIOLET);
		Render.endMode3D();
		Scene.draw(scene);
		Shape2D.drawRectangle(0, 0, 10, 10, Color.SKYBLUE);
		Shape2D.drawRectangleLines(0, 0, 10, 10, Color.LIME);
		Shape2D.drawLine(new Vec2(0, 0), new Vec2(10, 10), Color.WHITE);
		Shape2D.drawCircle(new Vec2(5, 5), 4, Color.GOLD);
		Shape2D.drawCircleLines(new Vec2(5, 5), 4, Color.VIOLET);
		Shape2D.drawTriangle(new Vec2(0, 0), new Vec2(10, 0), new Vec2(5, 8), Color.RED);
		Shape2D.drawRoundedRectangle(0, 0, 20, 10, 3, Color.SKYBLUE);
		Shape2D.drawRoundedRectangle(0, 0, 20, 10, 1, 2, 3, 4, Color.GOLD);
		Shape2D.drawBorder(0, 0, 20, 10, 1, 1, 1, 1, 2, 2, 2, 2, Color.LIGHTGRAY);
		Text.draw("built-in", 10, 10, 16, Color.BLACK);
		Text2D.draw(label);
		Render.endFrame();
	}

	#if !sys
	/**
		js has no host loop to drive — the guest ABI replaces it — so this entry exists
		only so `tools/check_binding.py` can type-check every assertion above against the js
		binding. A wrapper that compiles on hxcpp but not js fails here, not in an example.
	**/
	public static function main():Void {}
	#else
	public static function main():Void {
		// Handlers first, on purpose: wgr_init_values memsets wgrender's runtime, so
		// anything installed before it is wiped. Setting them after would be the safe
		// order and would test nothing; `frames > 0` below is what catches it, and it
		// caught nothing for as long as this ran in the safe order.
		Wgr.setInit(onInit);
		Wgr.setFrame(onFrame);
		final rc = Wgr.initValues(WIDTH, HEIGHT, "check", Msaa4x | Resizable);
		check(rc != Wgr.ERR_VERSION_MISMATCH, "initValues guards the version like the guest ABI does");
		check(rc == 0, "initValues succeeded");
		Wgr.run();

		check(frames > 0, "the loop ran at least one frame, with the handlers set before initValues");
		eq(fetchedUrl, "https://example.invalid/no-such3.png",
			"the program's fetcher got the task's own source, a URL host set later notwithstanding");
		Asset.setFetcher(null);
		say('${checks - failures}/$checks checks passed over $frames frames');
		Sys.exit(failures == 0 ? 0 : 1);
	}
	#end
}
