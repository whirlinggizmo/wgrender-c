#ifndef WGR_INTERNAL_MODULE_H
#define WGR_INTERNAL_MODULE_H

/* Optional subsystems (models, textures, sprites, particles, audio, ...) join the
 * runtime when a program uses them: each registers itself from a constructor in its
 * own source file (WGR_MODULE), which a static library links only when the program
 * references something in that file. The core (wgr.c, wgr_render, wgr_scene) never calls
 * them by name: it runs the registered modules' callbacks here, and reaches the few
 * services it needs through the hooks in wgr_render.h and wgr_scene.h. A program that
 * never draws a model doesn't link glTF parsing; one without sound links no audio
 * decoders (docs/TASKS.md: web size). */

typedef struct wgr_module {
    const char *name;
    int order;                /* init order among modules, lower first; deinit reversed */
    void (*init)(void);       /* after the core is up (graphics, scenes, files, assets) */
    void (*deinit)(void);     /* before the core goes */
    void (*begin_frame)(void); /* each frame, before the ticks (poll input devices) */
    void (*end_tick)(void);    /* after each tick (clear tick input edges) */
    void (*update)(float dt);  /* once a frame, after the ticks, before the frame callback */
    void (*frame_done)(void);  /* after the frame callback (clear frame input edges) */
    void (*flush)(void);      /* wgr_render_end: upload the frame's data, before any pass */
    void (*end_frame)(void);  /* wgr_render_end: after the frame is submitted */
    struct wgr_module *next;
} wgr_module_t;

/* From a constructor, before main(): keeps the list in init order. */
void wgr_module_register(wgr_module_t *module);

/* The runtime (wgr.c, wgr_render.c). init/deinit only for modules the runtime started;
 * flush and end_frame run for every linked module (tests start them by hand), which
 * must tolerate not having been initialized. */
void wgr_module_init_all(void);
void wgr_module_deinit_all(void);
void wgr_module_begin_frame_all(void);
void wgr_module_end_tick_all(void);
void wgr_module_update_all(float dt);
void wgr_module_frame_done_all(void);
void wgr_module_flush_all(void);
void wgr_module_end_frame_all(void);

/* The registered modules, in init order. For tests. */
const wgr_module_t *wgr_module_list(void);

/* Register `module` (a static wgr_module_t) when this file is linked. */
#if defined(_MSC_VER)
/* A function pointer in the C runtime's initializer section runs before main(). */
#pragma section(".CRT$XCU", read)
#define WGR_MODULE(module)                                                            \
    static void wgr_register_##module(void) { wgr_module_register(&module); }          \
    __declspec(allocate(".CRT$XCU")) void (*wgr_register_##module##_ptr)(void) = \
        wgr_register_##module
#else
#define WGR_MODULE(module)                                                      \
    __attribute__((constructor)) static void wgr_register_##module(void)        \
    {                                                                          \
        wgr_module_register(&module);                                           \
    }
#endif

#endif // WGR_INTERNAL_MODULE_H
