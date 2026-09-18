#ifndef SK_INTERNAL_AUDIO_H
#define SK_INTERNAL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include <sk_audio.h> /* the public header ("" would find this file); sk_audio_release lives there */
#include "sk_types.h"

/* Audio mixing runs on the audio device's thread (sokol_audio callback mode), so
 * Sound and Audio state shared with the game thread is guarded by one recursive
 * lock: take it (sk_audio_lock) around any change to a registered sk_sound_t or
 * to Audio resources. See docs/PLAN-audio.md. */

/* Decoder state for a Sound playing a streamed Audio (private to sk_audio.c). */
typedef struct sk_audio_stream sk_audio_stream_t;

/* Sound object: per-playback state that references a shared Audio resource.
 * Music is a looping sound. */
typedef struct {
    sk_handle_t audio;         /* Audio resource supplying the samples */
    double pos;                /* playback cursor in source frames */
    float volume;
    float pitch;
    float pan;                 /* -1 left .. 0 center .. 1 right (balance) */
    bool loop;
    bool playing;
    sk_audio_stream_t *stream; /* decoder, created by the mixer for streamed Audio */
} sk_sound_t;

void sk_audio_init(void);
void sk_audio_deinit(void);

void sk_audio_lock(void);
void sk_audio_unlock(void);

/* Audio resource reference counting (take the lock). Public creation/destruction
 * is sk_audio_create / sk_audio_release; Sound objects add their own references. */
void sk_audio_retain(sk_handle_t audio);

/* The sound slots, for the mixer to walk (take the lock): indices 1 up to
 * sk_sound_slot_count(), NULL for a free slot. Sounds are created and destroyed, and
 * their pool grows, under the lock. */
int sk_sound_slot_count(void);
sk_sound_t *sk_sound_slot(int index);

/* Free a sound's stream decoder, e.g. when it changes Audio or is destroyed
 * (take the lock). */
void sk_audio_stream_free(sk_sound_t *sound);

/* Streaming choice for sk_audio_create_mode. */
typedef enum {
    SK_AUDIO_MODE_AUTO = 0, /* stream files larger than SK_AUDIO_STREAM_MIN_BYTES */
    SK_AUDIO_MODE_DECODE,   /* decode fully at create */
    SK_AUDIO_MODE_STREAM,   /* keep the file, decode while playing */
} sk_audio_mode_t;

#define SK_AUDIO_STREAM_MIN_BYTES (1024 * 1024)

/* sk_audio_create with an explicit mode (tests compare the two). */
sk_handle_t sk_audio_create_mode(const char *path, sk_audio_mode_t mode);
bool sk_audio_is_streamed(sk_handle_t audio);

/* Mix every playing sound into `out` (stereo, interleaved, `frames` frames) at
 * `sample_rate`, overwriting it. The device callback calls this; tests call it
 * directly (headless builds have no audio device). Takes the lock. */
void sk_audio_mix(float *out, int frames, int sample_rate);

#endif // SK_INTERNAL_AUDIO_H
