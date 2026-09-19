#include "sk_sound.h"

#include <string.h>

#include "internal/exports.h"
#include "internal/sk_audio.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_module.h"
#include "sk_logger.h"

#define SOUNDS_INITIAL 32 /* slots to start with; the pool doubles as needed */

static sk_sound_t *sk_sounds; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_sound_pool;

static sk_sound_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_sound_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid sound handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_sounds[index];
}

/* Create a Sound object that plays `audio` (adds its own reference).
 * `audio` may be 0: create the sound now (set volume/pitch/loop, even play())
 * and attach the Audio later via sk_sound_set_audio(); the mixer skips a playing
 * sound with no Audio, then picks it up once the resource is set.
 * Sounds are shared with the mixer thread: every change takes sk_audio_lock. */
static sk_handle_t create_sound(sk_handle_t audio, bool loop)
{
    sk_handle_t handle;
    uint16_t index = 0;

    sk_audio_lock(); /* the mixer walks the pool, which may grow here */
    handle = sk_handle_pool_alloc(&sk_sound_pool);
    if (handle == 0) {
        sk_audio_unlock();
        log_error("sound: pool full (%u)", (unsigned)sk_sound_pool.max - 1u);
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
    sk_audio_unlock();
    return handle;
}

SK_KEEP
bool sk_sound_set_audio(sk_handle_t handle, sk_handle_t audio)
{
    sk_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    sk_audio_lock();
    if (sound_ptr->audio != audio) {
        if (audio != 0) sk_audio_retain(audio);
        sk_audio_stream_free(sound_ptr); /* the decoder belongs to the old Audio */
        if (sound_ptr->audio != 0) sk_audio_release(sound_ptr->audio);
        sound_ptr->audio = audio;
        /* volume/pitch/loop/playing/pos retained, so playback picks up the new Audio */
    }
    sk_audio_unlock();
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
    sk_audio_lock();
    sk_audio_stream_free(sound_ptr);
    audio = sound_ptr->audio;
    memset(sound_ptr, 0, sizeof(*sound_ptr));
    sk_handle_pool_free(&sk_sound_pool, handle);
    sk_audio_release(audio); /* frees the Audio once its last sound is gone */
    sk_audio_unlock();
}

/* Apply a change to a sound under the mixer lock. */
#define WITH_SOUND(handle, statement)                                                              \
    do {                                                                                           \
        sk_sound_t *sound_ptr = resolve(handle);                                                   \
        if (sound_ptr == NULL) return false;                                                       \
        sk_audio_lock();                                                                           \
        statement;                                                                                 \
        sk_audio_unlock();                                                                         \
        return true;                                                                               \
    } while (0)

SK_KEEP
bool sk_sound_play(sk_handle_t handle)
{
    WITH_SOUND(handle, { sound_ptr->pos = 0.0; sound_ptr->playing = true; });
}

SK_KEEP
bool sk_sound_pause(sk_handle_t handle)
{
    WITH_SOUND(handle, sound_ptr->playing = false); /* keep position */
}

SK_KEEP
bool sk_sound_resume(sk_handle_t handle)
{
    WITH_SOUND(handle, sound_ptr->playing = true); /* from current position */
}

SK_KEEP
bool sk_sound_stop(sk_handle_t handle)
{
    WITH_SOUND(handle, { sound_ptr->playing = false; sound_ptr->pos = 0.0; });
}

SK_KEEP
bool sk_sound_set_loop(sk_handle_t handle, bool loop)
{
    WITH_SOUND(handle, sound_ptr->loop = loop);
}

SK_KEEP
bool sk_sound_set_volume(sk_handle_t handle, float volume)
{
    WITH_SOUND(handle, sound_ptr->volume = volume);
}

SK_KEEP
bool sk_sound_set_pan(sk_handle_t handle, float pan)
{
    pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    WITH_SOUND(handle, sound_ptr->pan = pan);
}

SK_KEEP
bool sk_sound_set_pitch(sk_handle_t handle, float pitch)
{
    WITH_SOUND(handle, sound_ptr->pitch = pitch);
}

SK_KEEP
bool sk_sound_is_playing(sk_handle_t handle)
{
    sk_sound_t *sound_ptr = resolve(handle);
    bool playing;
    if (sound_ptr == NULL) return false;
    sk_audio_lock();
    playing = sound_ptr->playing; /* the mixer clears it when a sound ends */
    sk_audio_unlock();
    return playing;
}

void sk_sound_init(void)
{
    sk_audio_lock(); /* the mixer may be running already */
    if (!sk_handle_pool_init(&sk_sound_pool, SK_HANDLE_KIND_SOUND, "sound", (void **)&sk_sounds,
                             sizeof(sk_sound_t), SOUNDS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("sound: out of memory");
    }
    sk_audio_unlock();
}

void sk_sound_deinit(void)
{
    for (uint16_t i = 1; i < sk_sound_pool.capacity; i++) {
        if (sk_sound_pool.occupied[i]) {
            sk_audio_lock();
            sk_audio_stream_free(&sk_sounds[i]);
            sk_audio_release(sk_sounds[i].audio);
            sk_sounds[i] = (sk_sound_t){0};
            sk_audio_unlock();
        }
    }
    sk_audio_lock();
    sk_handle_pool_destroy(&sk_sound_pool);
    sk_audio_unlock();
}

int sk_sound_slot_count(void)
{
    return sk_sound_pool.capacity;
}

sk_sound_t *sk_sound_slot(int index)
{
    return index > 0 && index < sk_sound_pool.capacity && sk_sound_pool.occupied[index] ? &sk_sounds[index] : NULL;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/sk_module.h). */
static sk_module_t sk_sound_module = {.name = "sound", .order = 91, .init = sk_sound_init, .deinit = sk_sound_deinit};
SK_MODULE(sk_sound_module)
