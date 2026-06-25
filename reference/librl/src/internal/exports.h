#ifndef EXPORTS_H
#define EXPORTS_H

#ifdef PLATFORM_WEB
    #include <emscripten.h>
    #define RL_KEEP EMSCRIPTEN_KEEPALIVE
#else // empty stubs
    #define RL_KEEP
#endif

#endif // EXPORTS_H
