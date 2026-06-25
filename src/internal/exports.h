#ifndef SK_INTERNAL_EXPORTS_H
#define SK_INTERNAL_EXPORTS_H

#if defined(PLATFORM_WEB) || defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #define SK_KEEP EMSCRIPTEN_KEEPALIVE
#else // empty stub on native targets
    #define SK_KEEP
#endif

#endif // SK_INTERNAL_EXPORTS_H
