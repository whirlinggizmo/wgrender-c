#include "sk_event.h"

#include "internal/exports.h"
#include "internal/sk_internal.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Compact flat-array event bus. Sufficient for the modest listener counts this
 * library deals with; can be swapped for a hashed bus later without changing the
 * public API. */

#define SK_EVENT_MAX_NAME 64

typedef struct {
    char name[SK_EVENT_MAX_NAME];
    sk_event_listener_fn fn;
    void *user_data;
    bool once;
    bool active;
} sk_event_listener_t;

typedef struct {
    sk_event_listener_t *items;
    int count;
    int capacity;
} sk_event_bus_t;

static sk_event_bus_t *sk_bus = NULL;

static bool name_eq(const char *a, const char *b)
{
    return a != NULL && b != NULL && strncmp(a, b, SK_EVENT_MAX_NAME) == 0;
}

static bool ensure_capacity(sk_event_bus_t *bus, int needed)
{
    int new_cap;
    sk_event_listener_t *grown;

    if (bus->capacity >= needed) {
        return true;
    }
    new_cap = bus->capacity > 0 ? bus->capacity * 2 : 8;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    grown = (sk_event_listener_t *)realloc(bus->items, (size_t)new_cap * sizeof(*grown));
    if (grown == NULL) {
        return false;
    }
    bus->items = grown;
    bus->capacity = new_cap;
    return true;
}

static int add_listener(const char *event_name,
                        sk_event_listener_fn listener,
                        void *user_data,
                        bool once)
{
    sk_event_listener_t *slot;

    if (sk_bus == NULL || event_name == NULL || listener == NULL) {
        return -1;
    }
    if (!ensure_capacity(sk_bus, sk_bus->count + 1)) {
        return -1;
    }

    slot = &sk_bus->items[sk_bus->count++];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->name, event_name, SK_EVENT_MAX_NAME - 1);
    slot->fn = listener;
    slot->user_data = user_data;
    slot->once = once;
    slot->active = true;
    return 0;
}

int sk_event_init(void)
{
    if (sk_bus != NULL) {
        return 0;
    }
    sk_bus = (sk_event_bus_t *)calloc(1, sizeof(*sk_bus));
    return sk_bus != NULL ? 0 : -1;
}

void sk_event_deinit(void)
{
    if (sk_bus == NULL) {
        return;
    }
    free(sk_bus->items);
    free(sk_bus);
    sk_bus = NULL;
}

SK_KEEP
int sk_event_on(const char *event_name, sk_event_listener_fn listener, void *user_data)
{
    return add_listener(event_name, listener, user_data, false);
}

SK_KEEP
int sk_event_once(const char *event_name, sk_event_listener_fn listener, void *user_data)
{
    return add_listener(event_name, listener, user_data, true);
}

SK_KEEP
int sk_event_off(const char *event_name, sk_event_listener_fn listener, void *user_data)
{
    int removed = 0;

    if (sk_bus == NULL) {
        return -1;
    }
    for (int i = 0; i < sk_bus->count; i++) {
        sk_event_listener_t *it = &sk_bus->items[i];
        if (it->active && name_eq(it->name, event_name) && it->fn == listener &&
            it->user_data == user_data) {
            it->active = false;
            removed++;
        }
    }
    return removed;
}

SK_KEEP
int sk_event_off_all(const char *event_name)
{
    int removed = 0;

    if (sk_bus == NULL) {
        return -1;
    }
    for (int i = 0; i < sk_bus->count; i++) {
        sk_event_listener_t *it = &sk_bus->items[i];
        if (it->active && name_eq(it->name, event_name)) {
            it->active = false;
            removed++;
        }
    }
    return removed;
}

SK_KEEP
int sk_event_emit(const char *event_name, void *payload)
{
    int fired = 0;
    int count;

    if (sk_bus == NULL) {
        return -1;
    }

    /* snapshot count: listeners added during emit do not fire this round */
    count = sk_bus->count;
    for (int i = 0; i < count; i++) {
        sk_event_listener_t *it = &sk_bus->items[i];
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

SK_KEEP
int sk_event_listener_count(const char *event_name)
{
    int n = 0;

    if (sk_bus == NULL) {
        return -1;
    }
    for (int i = 0; i < sk_bus->count; i++) {
        sk_event_listener_t *it = &sk_bus->items[i];
        if (it->active && name_eq(it->name, event_name)) {
            n++;
        }
    }
    return n;
}
