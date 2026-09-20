#ifndef WGRI_INTERNAL_AUDIO_H
#define WGRI_INTERNAL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "wgr_audio.h"
#include "wgr_types.h"

/* Audio mixing runs on the audio device's thread (sokol_audio callback mode), so
 * Sound and Audio state shared with the game thread is guarded by one recursive
 * lock: take it (wgri_audio_lock) around any change to a registered wgri_sound_t or
 * to Audio resources. See docs/PLAN-audio.md. */

/* Decoder state for a Sound playing a streamed Audio (private to wgr_audio.c). */
typedef struct wgri_audio_stream wgri_audio_stream_t;

/* Sound object: per-playback state that references a shared Audio resource.
 * Music is a looping sound. */
typedef struct {
    wgr_handle_t audio;         /* Audio resource supplying the samples */
    double pos;                /* playback cursor in source frames */
    float volume;
    float pitch;
    float pan;                 /* -1 left .. 0 center .. 1 right (balance) */
    bool loop;
    bool playing;
    wgri_audio_stream_t *stream; /* decoder, created by the mixer for streamed Audio */
} wgri_sound_t;

void wgri_audio_init(void);
void wgri_audio_deinit(void);

void wgri_audio_lock(void);
void wgri_audio_unlock(void);

/* Audio resource reference counting (take the lock). Public creation/destruction
 * is wgr_audio_create / wgr_audio_release; Sound objects add their own references. */
void wgri_audio_retain(wgr_handle_t audio);

/* The sound slots, for the mixer to walk (take the lock): indices 1 up to
 * wgri_sound_slot_count(), NULL for a free slot. Sounds are created and destroyed, and
 * their pool grows, under the lock. */
int wgri_sound_slot_count(void);
wgri_sound_t *wgri_sound_slot(int index);

/* Free a sound's stream decoder, e.g. when it changes Audio or is destroyed
 * (take the lock). */
void wgri_audio_stream_free(wgri_sound_t *sound);

/* Streaming choice for wgri_audio_create_mode. */
typedef enum {
    WGRI_AUDIO_MODE_AUTO = 0, /* stream files larger than WGRI_AUDIO_STREAM_MIN_BYTES */
    WGRI_AUDIO_MODE_DECODE,   /* decode fully at create */
    WGRI_AUDIO_MODE_STREAM,   /* keep the file, decode while playing */
} wgri_audio_mode_t;

#define WGRI_AUDIO_STREAM_MIN_BYTES (1024 * 1024)

/* wgr_audio_create with an explicit mode (tests compare the two). */
wgr_handle_t wgri_audio_create_mode(const char *path, wgri_audio_mode_t mode);
bool wgri_audio_is_streamed(wgr_handle_t audio);

/* Mix every playing sound into `out` (stereo, interleaved, `frames` frames) at
 * `sample_rate`, overwriting it. The device callback calls this; tests call it
 * directly (headless builds have no audio device). Takes the lock. */
void wgri_audio_mix(float *out, int frames, int sample_rate);

#endif // WGRI_INTERNAL_AUDIO_H
