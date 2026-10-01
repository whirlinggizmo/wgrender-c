package wgr.impl;

// Haxe picks Raw.js.hx or Raw.cpp.hx over this by target; this is what a third target
// would hit. Same pattern as librl's rl/impl/RLImpl.hx.
// `macro` is not a target: a macro that reaches this package must not trip it.
#if !macro
#error "wgr: no Raw implementation for this target. Add src/wgr/impl/Raw.<target>.hx."
#end
