// Haxe applies this to every module in this directory, so the public modules reach the
// C surface (and its struct and enum types) without each one repeating the import.
// Not in macro context: there is no Raw for eval, and the compile-time code under
// wgr.macros runs there and must not drag the per-target surface in with it.
#if !macro
import wgr.impl.Raw;
// and the callback plumbing, which Event and Asset reach for on both targets
import wgr.impl.Trampoline;
#end
