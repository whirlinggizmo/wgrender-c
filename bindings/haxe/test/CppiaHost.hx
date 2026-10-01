// check-cppia's executable: the whole binding, compiled `-D scriptable` (which fails on
// a public-class function with a C type in its signature), and a cppia module loaded
// into it (which fails on a wrapper the module can't call). test/check.py builds and
// runs it; the module is test/CppiaModule.hx with wgr.macros.Cppia's generated class.
class CppiaHost {
	static function main() {
		var path = Sys.args()[0];
		if (path == null) {
			Sys.println("check-cppia: give it the module to load");
			Sys.exit(2);
		}
		var module = try cpp.cppia.Module.fromData(sys.io.File.getBytes(path).getData()) catch (e) {
			Sys.println('check-cppia: can\'t load $path: $e');
			Sys.exit(1);
			null;
		}
		module.boot();
		module.run();
		Sys.println('check-cppia: $path loaded, every function linked');
	}
}
