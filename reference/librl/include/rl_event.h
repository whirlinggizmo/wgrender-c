#ifndef RL_EVENT_H
#define RL_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rl_event_listener_fn)(void *payload, void *user_data);

int rl_event_on(const char *event_name, rl_event_listener_fn listener, void *user_data);
int rl_event_once(const char *event_name, rl_event_listener_fn listener, void *user_data);
int rl_event_off(const char *event_name, rl_event_listener_fn listener, void *user_data);
int rl_event_off_all(const char *event_name);
int rl_event_emit(const char *event_name, void *payload);
int rl_event_listener_count(const char *event_name);

#ifdef __cplusplus
}
#endif

#endif // RL_EVENT_H
