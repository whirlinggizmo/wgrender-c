#include "rl_event.h"

#include "event/event.h"
#include "internal/exports.h"

static event_bus_t *rl_event_bus = 0;

int rl_event_init(void)
{
    if (rl_event_bus != 0) {
        return 0;
    }

    rl_event_bus = event_bus_create();
    return rl_event_bus != 0 ? 0 : -1;
}

void rl_event_deinit(void)
{
    if (rl_event_bus == 0) {
        return;
    }

    event_bus_destroy(rl_event_bus);
    rl_event_bus = 0;
}

RL_KEEP
int rl_event_on(const char *event_name, rl_event_listener_fn listener, void *user_data)
{
    if (rl_event_bus == 0) {
        return -1;
    }
    return event_bus_on(rl_event_bus, event_name, listener, user_data);
}

RL_KEEP
int rl_event_once(const char *event_name, rl_event_listener_fn listener, void *user_data)
{
    if (rl_event_bus == 0) {
        return -1;
    }
    return event_bus_once(rl_event_bus, event_name, listener, user_data);
}

RL_KEEP
int rl_event_off(const char *event_name, rl_event_listener_fn listener, void *user_data)
{
    if (rl_event_bus == 0) {
        return -1;
    }
    return event_bus_off(rl_event_bus, event_name, listener, user_data);
}

RL_KEEP
int rl_event_off_all(const char *event_name)
{
    if (rl_event_bus == 0) {
        return -1;
    }
    return event_bus_off_all(rl_event_bus, event_name);
}

RL_KEEP
int rl_event_emit(const char *event_name, void *payload)
{
    if (rl_event_bus == 0) {
        return -1;
    }
    return event_bus_emit(rl_event_bus, event_name, payload);
}

RL_KEEP
int rl_event_listener_count(const char *event_name)
{
    if (rl_event_bus == 0) {
        return -1;
    }
    return event_bus_listener_count(rl_event_bus, event_name);
}
