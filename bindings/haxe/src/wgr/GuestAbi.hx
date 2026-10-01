package wgr;

// As Raw.hx: GuestAbi.js.hx and GuestAbi.cpp.hx are chosen by target.
// `macro` is not a target: a macro that reaches this package must not trip it.
#if !macro
#error "wgr: no GuestAbi implementation for this target. Add src/wgr/GuestAbi.<target>.hx."
#end
