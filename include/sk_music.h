#ifndef SK_MUSIC_H
#define SK_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

sk_handle_t sk_music_create(const char *path);
sk_handle_t sk_music_create_from_memory(const unsigned char *data, int size, const char *hint);
void sk_music_destroy(sk_handle_t handle);
bool sk_music_play(sk_handle_t handle);
bool sk_music_pause(sk_handle_t handle);
bool sk_music_stop(sk_handle_t handle);
bool sk_music_set_loop(sk_handle_t handle, bool loop);
bool sk_music_set_volume(sk_handle_t handle, float volume);
bool sk_music_is_playing(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_MUSIC_H
