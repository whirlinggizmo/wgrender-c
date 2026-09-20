#include "internal/wgr_module_internal.h"

#include <stdbool.h>
#include <stddef.h>

/* Registered modules, in init order (registration runs before main(), one thread). */
static wgri_module_t *wgr_modules;
static bool wgr_modules_started;

void wgri_module_register(wgri_module_t *module)
{
    wgri_module_t **at = &wgr_modules;
    while (*at != NULL && (*at)->order <= module->order) {
        at = &(*at)->next;
    }
    module->next = *at;
    *at = module;
}

const wgri_module_t *wgri_module_list(void)
{
    return wgr_modules;
}

void wgri_module_init_all(void)
{
    for (wgri_module_t *m = wgr_modules; m != NULL; m = m->next) {
        if (m->init != NULL) m->init();
    }
    wgr_modules_started = true;
}

static void deinit_from(wgri_module_t *m)
{
    if (m == NULL) return;
    deinit_from(m->next); /* reverse order */
    if (m->deinit != NULL) m->deinit();
}

void wgri_module_deinit_all(void)
{
    if (!wgr_modules_started) return;
    deinit_from(wgr_modules);
    wgr_modules_started = false;
}

void wgri_module_begin_frame_all(void)
{
    for (wgri_module_t *m = wgr_modules; m != NULL; m = m->next) {
        if (m->begin_frame != NULL) m->begin_frame();
    }
}

void wgri_module_end_tick_all(void)
{
    for (wgri_module_t *m = wgr_modules; m != NULL; m = m->next) {
        if (m->end_tick != NULL) m->end_tick();
    }
}

void wgri_module_frame_done_all(void)
{
    for (wgri_module_t *m = wgr_modules; m != NULL; m = m->next) {
        if (m->frame_done != NULL) m->frame_done();
    }
}

void wgri_module_update_all(float dt)
{
    for (wgri_module_t *m = wgr_modules; m != NULL; m = m->next) {
        if (m->update != NULL) m->update(dt);
    }
}

void wgri_module_flush_all(void)
{
    for (wgri_module_t *m = wgr_modules; m != NULL; m = m->next) {
        if (m->flush != NULL) m->flush();
    }
}

void wgri_module_end_frame_all(void)
{
    for (wgri_module_t *m = wgr_modules; m != NULL; m = m->next) {
        if (m->end_frame != NULL) m->end_frame();
    }
}
