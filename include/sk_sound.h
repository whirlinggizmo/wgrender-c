#ifndef SK_SOUND_H
#define SK_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

sk_handle_t sk_sound_create(const char *path);
sk_handle_t sk_sound_create_from_memory(const unsigned char *data, int size, const char *hint);
void sk_sound_destroy(sk_handle_t handle);
bool sk_sound_play(sk_handle_t handle);
bool sk_sound_stop(sk_handle_t handle);
bool sk_sound_set_volume(sk_handle_t handle, float volume);
bool sk_sound_set_pitch(sk_handle_t handle, float pitch);
bool sk_sound_is_playing(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_SOUND_H
