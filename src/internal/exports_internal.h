#ifndef WGRI_INTERNAL_EXPORTS_H
#define WGRI_INTERNAL_EXPORTS_H

#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #define WGRI_KEEP EMSCRIPTEN_KEEPALIVE
#else // empty stub on native targets
    #define WGRI_KEEP
#endif

#endif // WGRI_INTERNAL_EXPORTS_H
