#ifndef SK_INTERNAL_AUDIO_H
#define SK_INTERNAL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "sk_types.h"

/* Sound object: per-playback state that references a shared Audio resource (the
 * decoded PCM). Both sound and music objects are sk_sound_t — "music" is just a
 * sound created with looping on. The mixer reads registered sounds each frame
 * (push model, main thread — no locking). */
typedef struct {
    sk_handle_t audio;     /* Audio resource supplying the PCM */
    double pos;            /* playback cursor in frames */
    float volume;
    float pitch;
    bool loop;
    bool playing;
} sk_sound_t;

void sk_audio_init(void);
void sk_audio_deinit(void);
void sk_audio_tick(void); /* mix + push one block; call each frame */

/* Audio resource reference counting. Public creation/destruction is in
 * include/sk_audio.h (sk_audio_create / sk_audio_destroy); Sound/Music objects
 * add and drop their own references here. */
void sk_audio_retain(sk_handle_t audio);
void sk_audio_release(sk_handle_t audio);

/* Register/unregister a Sound object with the mixer. */
void sk_audio_register(sk_sound_t *sound);
void sk_audio_unregister(sk_sound_t *sound);

#endif // SK_INTERNAL_AUDIO_H
