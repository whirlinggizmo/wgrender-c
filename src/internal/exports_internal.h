#ifndef WGRI_INTERNAL_EXPORTS_H
#define WGRI_INTERNAL_EXPORTS_H

/* WGRI_KEEP marks the public API. By default it is nothing: a program links what it
 * calls and the linker drops the rest, and a JS host lists what its guest calls
 * (wgrender-hx's WebHost), so nothing needs forcing. Built with -DWGR_EXPORT_FULL_API,
 * a web build keeps and exports every public function whose object is linked, for a
 * JS host that wants the whole API from the library without listing it. (Until
 * 2026-09-26 that was the default, carried over from librl, and it put every public
 * function of a linked object into every program and host.) wgrender's own programs
 * export the few functions its tools call from JS at link time instead (CMakeLists.txt,
 * WGR_TOOL_EXPORTS). */
#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    /* A C function wgrender's own JS calls (an EM_JS body): kept always, whatever
     * WGRI_KEEP is, since nothing in C references it for the linker to see. */
    #define WGRI_JS_CALLED EMSCRIPTEN_KEEPALIVE
#else
    #define WGRI_JS_CALLED
#endif

#if defined(WGR_EXPORT_FULL_API) && (defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__))
    #include <emscripten.h>
    #define WGRI_KEEP EMSCRIPTEN_KEEPALIVE
#else
    #define WGRI_KEEP
#endif

#endif // WGRI_INTERNAL_EXPORTS_H
