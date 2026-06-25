#ifndef SK_INTERNAL_AUDIO_H
#define SK_INTERNAL_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/* A decoded PCM voice. Sound and music handles each own one. The mixer reads
 * registered voices each frame (push model, main thread — no locking). */
typedef struct {
    float *pcm;            /* interleaved float samples */
    uint64_t frame_count;  /* frames (samples per channel) */
    int channels;          /* 1 or 2 */
    int sample_rate;       /* source rate */
    double pos;            /* playback cursor in frames */
    float volume;
    float pitch;
    bool loop;
    bool playing;
} sk_voice_t;

void sk_audio_init(void);
void sk_audio_deinit(void);
void sk_audio_tick(void); /* mix + push one block; call each frame */

/* Decode an in-memory audio file (mp3/ogg/wav, chosen by `hint` extension or
 * content) into `out`. Returns false on failure. */
bool sk_audio_decode(const unsigned char *data, int size, const char *hint, sk_voice_t *out);
void sk_voice_free(sk_voice_t *voice);

/* Register/unregister a voice with the mixer. */
void sk_audio_register(sk_voice_t *voice);
void sk_audio_unregister(sk_voice_t *voice);

#endif // SK_INTERNAL_AUDIO_H
