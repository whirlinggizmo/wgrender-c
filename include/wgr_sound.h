#ifndef WGR_SOUND_H
#define WGR_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "wgr_types.h"

/* Sound object (kind SOUND): a playable instance of an Audio resource. One-shot
 * sfx and looping background music are both Sounds — looping is just a flag, and
 * streamed-vs-decoded is a property of the Audio (see docs/ARCHITECTURE.md). */
wgr_handle_t wgr_sound_create(wgr_handle_t audio); /* audio may be 0 (attach later) */
bool wgr_sound_set_audio(wgr_handle_t handle, wgr_handle_t audio);
void wgr_sound_destroy(wgr_handle_t handle);
bool wgr_sound_play(wgr_handle_t handle);    /* (re)start from the beginning */
bool wgr_sound_pause(wgr_handle_t handle);   /* stop, keep position          */
bool wgr_sound_resume(wgr_handle_t handle);  /* play from current position   */
bool wgr_sound_stop(wgr_handle_t handle);    /* stop and rewind              */
bool wgr_sound_set_loop(wgr_handle_t handle, bool loop);
bool wgr_sound_set_volume(wgr_handle_t handle, float volume);
bool wgr_sound_set_pitch(wgr_handle_t handle, float pitch);
/* -1 = left only, 0 = centered (default), 1 = right only: the other side fades (balance). */
bool wgr_sound_set_pan(wgr_handle_t handle, float pan);
bool wgr_sound_is_playing(wgr_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // WGR_SOUND_H
