#ifndef SK_SOUND_H
#define SK_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* Sound object (kind SOUND): a playable instance of an Audio resource. One-shot
 * sfx and looping background music are both Sounds — looping is just a flag, and
 * streamed-vs-decoded is a property of the Audio (see docs/ARCHITECTURE.md). */
sk_handle_t sk_sound_create(sk_handle_t audio);
void sk_sound_destroy(sk_handle_t handle);
bool sk_sound_play(sk_handle_t handle);    /* (re)start from the beginning */
bool sk_sound_pause(sk_handle_t handle);   /* stop, keep position          */
bool sk_sound_resume(sk_handle_t handle);  /* play from current position   */
bool sk_sound_stop(sk_handle_t handle);    /* stop and rewind              */
bool sk_sound_set_loop(sk_handle_t handle, bool loop);
bool sk_sound_set_volume(sk_handle_t handle, float volume);
bool sk_sound_set_pitch(sk_handle_t handle, float pitch);
bool sk_sound_is_playing(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_SOUND_H
