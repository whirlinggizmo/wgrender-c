#ifndef WGR_EVENT_H
#define WGR_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wgr_event_listener_fn)(void *payload, void *user_data);

int wgr_event_on(const char *event_name, wgr_event_listener_fn listener, void *user_data);
int wgr_event_once(const char *event_name, wgr_event_listener_fn listener, void *user_data);
int wgr_event_off(const char *event_name, wgr_event_listener_fn listener, void *user_data);
int wgr_event_off_all(const char *event_name);
int wgr_event_emit(const char *event_name, void *payload);
int wgr_event_listener_count(const char *event_name);

#ifdef __cplusplus
}
#endif

#endif // WGR_EVENT_H
