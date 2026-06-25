#ifndef SK_EVENT_H
#define SK_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sk_event_listener_fn)(void *payload, void *user_data);

int sk_event_on(const char *event_name, sk_event_listener_fn listener, void *user_data);
int sk_event_once(const char *event_name, sk_event_listener_fn listener, void *user_data);
int sk_event_off(const char *event_name, sk_event_listener_fn listener, void *user_data);
int sk_event_off_all(const char *event_name);
int sk_event_emit(const char *event_name, void *payload);
int sk_event_listener_count(const char *event_name);

#ifdef __cplusplus
}
#endif

#endif // SK_EVENT_H
