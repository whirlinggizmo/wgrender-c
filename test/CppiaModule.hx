// check-cppia's module: nothing of its own. What it carries is WgrCppiaReach, which
// `--macro wgr.macros.Cppia.reachAll()` defines to call every public function of the
// binding, and which links against the executable's copies when CppiaHost loads it.
class CppiaModule {
	static function main() {}
}
