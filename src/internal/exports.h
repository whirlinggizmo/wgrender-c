#ifndef WGR_INTERNAL_EXPORTS_H
#define WGR_INTERNAL_EXPORTS_H

#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #define WGR_KEEP EMSCRIPTEN_KEEPALIVE
#else // empty stub on native targets
    #define WGR_KEEP
#endif

#endif // WGR_INTERNAL_EXPORTS_H
