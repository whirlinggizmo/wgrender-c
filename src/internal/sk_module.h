#ifndef SK_INTERNAL_MODULE_H
#define SK_INTERNAL_MODULE_H

/* Optional subsystems (models, textures, sprites, particles, audio, ...) join the
 * runtime when a program uses them: each registers itself from a constructor in its
 * own source file (SK_MODULE), which a static library links only when the program
 * references something in that file. The core (sk.c, sk_render, sk_scene) never calls
 * them by name: it runs the registered modules' callbacks here, and reaches the few
 * services it needs through the hooks in sk_render.h and sk_scene.h. A program that
 * never draws a model doesn't link glTF parsing; one without sound links no audio
 * decoders (docs/TASKS.md: web size). */

typedef struct sk_module {
    const char *name;
    int order;                /* init order among modules, lower first; deinit reversed */
    void (*init)(void);       /* after the core is up (graphics, scenes, files, assets) */
    void (*deinit)(void);     /* before the core goes */
    void (*update)(float dt); /* once a frame, after the ticks, before the frame callback */
    void (*flush)(void);      /* sk_render_end: upload the frame's data, before any pass */
    void (*end_frame)(void);  /* sk_render_end: after the frame is submitted */
    struct sk_module *next;
} sk_module_t;

/* From a constructor, before main(): keeps the list in init order. */
void sk_module_register(sk_module_t *module);

/* The runtime (sk.c, sk_render.c). init/deinit only for modules the runtime started;
 * flush and end_frame run for every linked module (tests start them by hand), which
 * must tolerate not having been initialized. */
void sk_module_init_all(void);
void sk_module_deinit_all(void);
void sk_module_update_all(float dt);
void sk_module_flush_all(void);
void sk_module_end_frame_all(void);

/* The registered modules, in init order. For tests. */
const sk_module_t *sk_module_list(void);

/* Register `module` (a static sk_module_t) when this file is linked. */
#if defined(_MSC_VER)
/* A function pointer in the C runtime's initializer section runs before main(). */
#pragma section(".CRT$XCU", read)
#define SK_MODULE(module)                                                            \
    static void sk_register_##module(void) { sk_module_register(&module); }          \
    __declspec(allocate(".CRT$XCU")) void (*sk_register_##module##_ptr)(void) = \
        sk_register_##module
#else
#define SK_MODULE(module)                                                      \
    __attribute__((constructor)) static void sk_register_##module(void)        \
    {                                                                          \
        sk_module_register(&module);                                           \
    }
#endif

#endif // SK_INTERNAL_MODULE_H
