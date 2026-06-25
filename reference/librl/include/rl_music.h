#ifndef RL_MUSIC_H
#define RL_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "rl_types.h"

rl_handle_t rl_music_create(const char *filename);
void rl_music_destroy(rl_handle_t handle);
bool rl_music_play(rl_handle_t handle);
bool rl_music_pause(rl_handle_t handle);
bool rl_music_stop(rl_handle_t handle);
bool rl_music_set_loop(rl_handle_t handle, bool loop);
bool rl_music_set_volume(rl_handle_t handle, float volume);
bool rl_music_is_playing(rl_handle_t handle);
bool rl_music_update(rl_handle_t handle);
void rl_music_update_all(void);

#ifdef __cplusplus
}
#endif

#endif // RL_MUSIC_H
