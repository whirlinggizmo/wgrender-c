#include "wgr_event.h"

#include "internal/exports.h"
#include "internal/wgr_internal.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Compact flat-array event bus. Sufficient for the modest listener counts this
 * library deals with; can be swapped for a hashed bus later without changing the
 * public API. */

#define WGR_EVENT_MAX_NAME 64

typedef struct {
    char name[WGR_EVENT_MAX_NAME];
    wgr_event_listener_fn fn;
    void *user_data;
    bool once;
    bool active;
} wgr_event_listener_t;

typedef struct {
    wgr_event_listener_t *items;
    int count;
    int capacity;
} wgr_event_bus_t;

static wgr_event_bus_t *wgr_bus = NULL;

static bool name_eq(const char *a, const char *b)
{
    return a != NULL && b != NULL && strncmp(a, b, WGR_EVENT_MAX_NAME) == 0;
}

static bool ensure_capacity(wgr_event_bus_t *bus, int needed)
{
    int new_cap;
    wgr_event_listener_t *grown;

    if (bus->capacity >= needed) {
        return true;
    }
    new_cap = bus->capacity > 0 ? bus->capacity * 2 : 8;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    grown = (wgr_event_listener_t *)realloc(bus->items, (size_t)new_cap * sizeof(*grown));
    if (grown == NULL) {
        return false;
    }
    bus->items = grown;
    bus->capacity = new_cap;
    return true;
}

static int add_listener(const char *event_name,
                        wgr_event_listener_fn listener,
                        void *user_data,
                        bool once)
{
    wgr_event_listener_t *slot;

    if (wgr_bus == NULL || event_name == NULL || listener == NULL) {
        return -1;
    }
    if (!ensure_capacity(wgr_bus, wgr_bus->count + 1)) {
        return -1;
    }

    slot = &wgr_bus->items[wgr_bus->count++];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->name, event_name, WGR_EVENT_MAX_NAME - 1);
    slot->fn = listener;
    slot->user_data = user_data;
    slot->once = once;
    slot->active = true;
    return 0;
}

int wgr_event_init(void)
{
    if (wgr_bus != NULL) {
        return 0;
    }
    wgr_bus = (wgr_event_bus_t *)calloc(1, sizeof(*wgr_bus));
    return wgr_bus != NULL ? 0 : -1;
}

void wgr_event_deinit(void)
{
    if (wgr_bus == NULL) {
        return;
    }
    free(wgr_bus->items);
    free(wgr_bus);
    wgr_bus = NULL;
}

WGR_KEEP
int wgr_event_on(const char *event_name, wgr_event_listener_fn listener, void *user_data)
{
    return add_listener(event_name, listener, user_data, false);
}

WGR_KEEP
int wgr_event_once(const char *event_name, wgr_event_listener_fn listener, void *user_data)
{
    return add_listener(event_name, listener, user_data, true);
}

WGR_KEEP
int wgr_event_off(const char *event_name, wgr_event_listener_fn listener, void *user_data)
{
    int removed = 0;

    if (wgr_bus == NULL) {
        return -1;
    }
    for (int i = 0; i < wgr_bus->count; i++) {
        wgr_event_listener_t *it = &wgr_bus->items[i];
        if (it->active && name_eq(it->name, event_name) && it->fn == listener &&
            it->user_data == user_data) {
            it->active = false;
            removed++;
        }
    }
    return removed;
}

WGR_KEEP
int wgr_event_off_all(const char *event_name)
{
    int removed = 0;

    if (wgr_bus == NULL) {
        return -1;
    }
    for (int i = 0; i < wgr_bus->count; i++) {
        wgr_event_listener_t *it = &wgr_bus->items[i];
        if (it->active && name_eq(it->name, event_name)) {
            it->active = false;
            removed++;
        }
    }
    return removed;
}

WGR_KEEP
int wgr_event_emit(const char *event_name, void *payload)
{
    int fired = 0;
    int count;

    if (wgr_bus == NULL) {
        return -1;
    }

    /* snapshot count: listeners added during emit do not fire this round */
    count = wgr_bus->count;
    for (int i = 0; i < count; i++) {
        wgr_event_listener_t *it = &wgr_bus->items[i];
        if (it->active && name_eq(it->name, event_name)) {
            it->fn(payload, it->user_data);
            fired++;
            if (it->once) {
                it->active = false;
            }
        }
    }
    return fired;
}

WGR_KEEP
int wgr_event_listener_count(const char *event_name)
{
    int n = 0;

    if (wgr_bus == NULL) {
        return -1;
    }
    for (int i = 0; i < wgr_bus->count; i++) {
        wgr_event_listener_t *it = &wgr_bus->items[i];
        if (it->active && name_eq(it->name, event_name)) {
            n++;
        }
    }
    return n;
}
