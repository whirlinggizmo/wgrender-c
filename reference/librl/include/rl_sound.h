#ifndef RL_SOUND_H
#define RL_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "rl_types.h"

rl_handle_t rl_sound_create(const char *filename);
void rl_sound_destroy(rl_handle_t handle);
bool rl_sound_play(rl_handle_t handle);
bool rl_sound_pause(rl_handle_t handle);
bool rl_sound_resume(rl_handle_t handle);
bool rl_sound_stop(rl_handle_t handle);
bool rl_sound_set_volume(rl_handle_t handle, float volume);
bool rl_sound_set_pitch(rl_handle_t handle, float pitch);
bool rl_sound_set_pan(rl_handle_t handle, float pan);
bool rl_sound_is_playing(rl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // RL_SOUND_H
