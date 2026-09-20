#ifndef WGR_INTERNAL_AUDIO_H
#define WGR_INTERNAL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "wgr_audio.h"
#include "wgr_types.h"

/* Audio mixing runs on the audio device's thread (sokol_audio callback mode), so
 * Sound and Audio state shared with the game thread is guarded by one recursive
 * lock: take it (wgr_audio_lock) around any change to a registered wgr_sound_t or
 * to Audio resources. See docs/PLAN-audio.md. */

/* Decoder state for a Sound playing a streamed Audio (private to wgr_audio.c). */
typedef struct wgr_audio_stream wgr_audio_stream_t;

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
    wgr_audio_stream_t *stream; /* decoder, created by the mixer for streamed Audio */
} wgr_sound_t;

void wgr_audio_init(void);
void wgr_audio_deinit(void);

void wgr_audio_lock(void);
void wgr_audio_unlock(void);

/* Audio resource reference counting (take the lock). Public creation/destruction
 * is wgr_audio_create / wgr_audio_release; Sound objects add their own references. */
void wgr_audio_retain(wgr_handle_t audio);

/* The sound slots, for the mixer to walk (take the lock): indices 1 up to
 * wgr_sound_slot_count(), NULL for a free slot. Sounds are created and destroyed, and
 * their pool grows, under the lock. */
int wgr_sound_slot_count(void);
wgr_sound_t *wgr_sound_slot(int index);

/* Free a sound's stream decoder, e.g. when it changes Audio or is destroyed
 * (take the lock). */
void wgr_audio_stream_free(wgr_sound_t *sound);

/* Streaming choice for wgr_audio_create_mode. */
typedef enum {
    WGR_AUDIO_MODE_AUTO = 0, /* stream files larger than WGR_AUDIO_STREAM_MIN_BYTES */
    WGR_AUDIO_MODE_DECODE,   /* decode fully at create */
    WGR_AUDIO_MODE_STREAM,   /* keep the file, decode while playing */
} wgr_audio_mode_t;

#define WGR_AUDIO_STREAM_MIN_BYTES (1024 * 1024)

/* wgr_audio_create with an explicit mode (tests compare the two). */
wgr_handle_t wgr_audio_create_mode(const char *path, wgr_audio_mode_t mode);
bool wgr_audio_is_streamed(wgr_handle_t audio);

/* Mix every playing sound into `out` (stereo, interleaved, `frames` frames) at
 * `sample_rate`, overwriting it. The device callback calls this; tests call it
 * directly (headless builds have no audio device). Takes the lock. */
void wgr_audio_mix(float *out, int frames, int sample_rate);

#endif // WGR_INTERNAL_AUDIO_H
