#include "sk_sound.h"

#include <string.h>

#include "internal/exports.h"
#include "internal/sk_audio.h"
#include "internal/sk_handle_pool.h"
#include "sk_logger.h"

#define MAX_SOUNDS 256

static sk_sound_t sk_sounds[MAX_SOUNDS];
static sk_handle_pool_t sk_sound_pool;
static uint16_t sk_sound_free_indices[MAX_SOUNDS];
static uint16_t sk_sound_generations[MAX_SOUNDS];
static unsigned char sk_sound_occupied[MAX_SOUNDS];

static sk_sound_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_sound_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid sound handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_sounds[index];
}

/* Create a Sound object that plays `audio` (consumes one reference to it).
 * `audio` may be 0 — create the sound now (set volume/pitch/loop, even play())
 * and attach the Audio later via sk_sound_set_audio(); the mixer skips a playing
 * sound with no PCM, then picks it up once the resource is set. */
static sk_handle_t create_sound(sk_handle_t audio, bool loop)
{
    sk_handle_t handle;
    uint16_t index = 0;

    handle = sk_handle_pool_alloc(&sk_sound_pool);
    if (handle == 0) {
        log_error("MAX_SOUNDS reached (%d)", MAX_SOUNDS);
        return 0;
    }
    sk_handle_pool_resolve(&sk_sound_pool, handle, &index);
    sk_sounds[index] = (sk_sound_t){
        .audio = audio,
        .volume = 1.0f,
        .pitch = 1.0f,
        .loop = loop,
        .playing = false,
    };
    if (audio != 0) sk_audio_retain(audio); /* the sound's own ref to the shared Audio */
    sk_audio_register(&sk_sounds[index]);
    return handle;
}

SK_KEEP
bool sk_sound_set_audio(sk_handle_t handle, sk_handle_t audio)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    if (sound_ptr->audio == audio) return true;
    if (sound_ptr->audio != 0) sk_audio_release(sound_ptr->audio);
    sound_ptr->audio = audio;
    if (audio != 0) sk_audio_retain(audio);
    /* volume/pitch/loop/playing/pos retained, so playback picks up the new PCM */
    return true;
}

SK_KEEP
sk_handle_t sk_sound_create(sk_handle_t audio)
{
    return create_sound(audio, false);
}

SK_KEEP
void sk_sound_destroy(sk_handle_t handle)
{
    sk_sound_t *sound_ptr = resolve(handle);
    sk_handle_t audio;
    if (sound_ptr == NULL) return;
    sk_audio_unregister(sound_ptr);
    audio = sound_ptr->audio;
    memset(sound_ptr, 0, sizeof(*sound_ptr));
    sk_handle_pool_free(&sk_sound_pool, handle);
    sk_audio_release(audio); /* frees the PCM once its last sound is gone */
}

SK_KEEP
bool sk_sound_play(sk_handle_t handle)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sound_ptr->pos = 0.0;
    sound_ptr->playing = true;
    return true;
}

SK_KEEP
bool sk_sound_pause(sk_handle_t handle)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sound_ptr->playing = false; /* keep position */
    return true;
}

SK_KEEP
bool sk_sound_resume(sk_handle_t handle)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sound_ptr->playing = true; /* from current position */
    return true;
}

SK_KEEP
bool sk_sound_stop(sk_handle_t handle)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sound_ptr->playing = false;
    sound_ptr->pos = 0.0;
    return true;
}

SK_KEEP
bool sk_sound_set_loop(sk_handle_t handle, bool loop)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sound_ptr->loop = loop;
    return true;
}

SK_KEEP
bool sk_sound_set_volume(sk_handle_t handle, float volume)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sound_ptr->volume = volume;
    return true;
}

SK_KEEP
bool sk_sound_set_pitch(sk_handle_t handle, float pitch)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sound_ptr->pitch = pitch;
    return true;
}

SK_KEEP
bool sk_sound_is_playing(sk_handle_t handle)
{
    sk_sound_t *sound_ptr = resolve(handle);
    return sound_ptr != NULL && sound_ptr->playing;
}

void sk_sound_init(void)
{
    memset(sk_sounds, 0, sizeof(sk_sounds));
    sk_handle_pool_init(&sk_sound_pool, SK_HANDLE_KIND_SOUND, MAX_SOUNDS,
                        sk_sound_free_indices, MAX_SOUNDS,
                        sk_sound_generations, sk_sound_occupied);
}

void sk_sound_deinit(void)
{
    for (uint16_t i = 1; i < MAX_SOUNDS; i++) {
        if (sk_sound_occupied[i]) {
            sk_audio_unregister(&sk_sounds[i]);
            sk_audio_release(sk_sounds[i].audio);
            sk_sounds[i] = (sk_sound_t){0};
        }
    }
    sk_handle_pool_reset(&sk_sound_pool);
}
