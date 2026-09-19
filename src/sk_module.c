#include "internal/sk_module.h"

#include <stdbool.h>
#include <stddef.h>

/* Registered modules, in init order (registration runs before main(), one thread). */
static sk_module_t *sk_modules;
static bool sk_modules_started;

void sk_module_register(sk_module_t *module)
{
    sk_module_t **at = &sk_modules;
    while (*at != NULL && (*at)->order <= module->order) {
        at = &(*at)->next;
    }
    module->next = *at;
    *at = module;
}

const sk_module_t *sk_module_list(void)
{
    return sk_modules;
}

void sk_module_init_all(void)
{
    for (sk_module_t *m = sk_modules; m != NULL; m = m->next) {
        if (m->init != NULL) m->init();
    }
    sk_modules_started = true;
}

static void deinit_from(sk_module_t *m)
{
    if (m == NULL) return;
    deinit_from(m->next); /* reverse order */
    if (m->deinit != NULL) m->deinit();
}

void sk_module_deinit_all(void)
{
    if (!sk_modules_started) return;
    deinit_from(sk_modules);
    sk_modules_started = false;
}

void sk_module_update_all(float dt)
{
    for (sk_module_t *m = sk_modules; m != NULL; m = m->next) {
        if (m->update != NULL) m->update(dt);
    }
}

void sk_module_flush_all(void)
{
    for (sk_module_t *m = sk_modules; m != NULL; m = m->next) {
        if (m->flush != NULL) m->flush();
    }
}

void sk_module_end_frame_all(void)
{
    for (sk_module_t *m = sk_modules; m != NULL; m = m->next) {
        if (m->end_frame != NULL) m->end_frame();
    }
}
