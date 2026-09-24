#ifndef WGRI_INTERNAL_EXPORTS_H
#define WGRI_INTERNAL_EXPORTS_H

/* WGRI_KEEP keeps a public function in a web build even when nothing in the program
 * calls it, and exports it. That is what a library archive wants: an object is only
 * linked when something references it. A program that compiles wgrender's sources in
 * directly (wgrender-nim does, from mk/build.json) links every object, so it defines
 * WGRI_KEEP empty (-DWGRI_KEEP=) and lets the linker drop what it never calls. */
#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    /* A C function wgrender's own JS calls (an EM_JS body): kept always, whatever
     * WGRI_KEEP is, since nothing in C references it for the linker to see. */
    #define WGRI_JS_CALLED EMSCRIPTEN_KEEPALIVE
#else
    #define WGRI_JS_CALLED
#endif

#ifndef WGRI_KEEP
#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #define WGRI_KEEP EMSCRIPTEN_KEEPALIVE
#else // empty stub on native targets
    #define WGRI_KEEP
#endif
#endif

#endif // WGRI_INTERNAL_EXPORTS_H
