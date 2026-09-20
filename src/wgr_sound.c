#include "wgr_sound.h"

#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_audio_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_module_internal.h"
#include "wgr_logger.h"

#define SOUNDS_INITIAL 32 /* slots to start with; the pool doubles as needed */

static wgri_sound_t *wgr_sounds; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_sound_pool;

static wgri_sound_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_sound_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid sound handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &wgr_sounds[index];
}

/* Create a Sound object that plays `audio` (adds its own reference).
 * `audio` may be 0: create the sound now (set volume/pitch/loop, even play())
 * and attach the Audio later via wgr_sound_set_audio(); the mixer skips a playing
 * sound with no Audio, then picks it up once the resource is set.
 * Sounds are shared with the mixer thread: every change takes wgri_audio_lock. */
static wgr_handle_t create_sound(wgr_handle_t audio, bool loop)
{
    wgr_handle_t handle;
    uint16_t index = 0;

    wgri_audio_lock(); /* the mixer walks the pool, which may grow here */
    handle = wgri_handle_pool_alloc(&wgr_sound_pool);
    if (handle == 0) {
        wgri_audio_unlock();
        log_error("sound: pool full (%u)", (unsigned)wgr_sound_pool.max - 1u);
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_sound_pool, handle, &index);
    wgr_sounds[index] = (wgri_sound_t){
        .audio = audio,
        .volume = 1.0f,
        .pitch = 1.0f,
        .loop = loop,
        .playing = false,
    };
    if (audio != 0) wgri_audio_retain(audio); /* the sound's own ref to the shared Audio */
    wgri_audio_unlock();
    return handle;
}

WGRI_KEEP
bool wgr_sound_set_audio(wgr_handle_t handle, wgr_handle_t audio)
{
    wgri_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    wgri_audio_lock();
    if (sound_ptr->audio != audio) {
        if (audio != 0) wgri_audio_retain(audio);
        wgri_audio_stream_free(sound_ptr); /* the decoder belongs to the old Audio */
        if (sound_ptr->audio != 0) wgr_audio_release(sound_ptr->audio);
        sound_ptr->audio = audio;
        /* volume/pitch/loop/playing/pos retained, so playback picks up the new Audio */
    }
    wgri_audio_unlock();
    return true;
}

WGRI_KEEP
wgr_handle_t wgr_sound_create(wgr_handle_t audio)
{
    return create_sound(audio, false);
}

WGRI_KEEP
void wgr_sound_destroy(wgr_handle_t handle)
{
    wgri_sound_t *sound_ptr = resolve(handle);
    wgr_handle_t audio;
    if (sound_ptr == NULL) return;
    wgri_audio_lock();
    wgri_audio_stream_free(sound_ptr);
    audio = sound_ptr->audio;
    memset(sound_ptr, 0, sizeof(*sound_ptr));
    wgri_handle_pool_free(&wgr_sound_pool, handle);
    wgr_audio_release(audio); /* frees the Audio once its last sound is gone */
    wgri_audio_unlock();
}

/* Apply a change to a sound under the mixer lock. */
#define WITH_SOUND(handle, statement)                                                              \
    do {                                                                                           \
        wgri_sound_t *sound_ptr = resolve(handle);                                                   \
        if (sound_ptr == NULL) return false;                                                       \
        wgri_audio_lock();                                                                           \
        statement;                                                                                 \
        wgri_audio_unlock();                                                                         \
        return true;                                                                               \
    } while (0)

WGRI_KEEP
bool wgr_sound_play(wgr_handle_t handle)
{
    WITH_SOUND(handle, { sound_ptr->pos = 0.0; sound_ptr->playing = true; });
}

WGRI_KEEP
bool wgr_sound_pause(wgr_handle_t handle)
{
    WITH_SOUND(handle, sound_ptr->playing = false); /* keep position */
}

WGRI_KEEP
bool wgr_sound_resume(wgr_handle_t handle)
{
    WITH_SOUND(handle, sound_ptr->playing = true); /* from current position */
}

WGRI_KEEP
bool wgr_sound_stop(wgr_handle_t handle)
{
    WITH_SOUND(handle, { sound_ptr->playing = false; sound_ptr->pos = 0.0; });
}

WGRI_KEEP
bool wgr_sound_set_loop(wgr_handle_t handle, bool loop)
{
    WITH_SOUND(handle, sound_ptr->loop = loop);
}

WGRI_KEEP
bool wgr_sound_set_volume(wgr_handle_t handle, float volume)
{
    WITH_SOUND(handle, sound_ptr->volume = volume);
}

WGRI_KEEP
bool wgr_sound_set_pan(wgr_handle_t handle, float pan)
{
    pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    WITH_SOUND(handle, sound_ptr->pan = pan);
}

WGRI_KEEP
bool wgr_sound_set_pitch(wgr_handle_t handle, float pitch)
{
    WITH_SOUND(handle, sound_ptr->pitch = pitch);
}

WGRI_KEEP
bool wgr_sound_is_playing(wgr_handle_t handle)
{
    wgri_sound_t *sound_ptr = resolve(handle);
    bool playing;
    if (sound_ptr == NULL) return false;
    wgri_audio_lock();
    playing = sound_ptr->playing; /* the mixer clears it when a sound ends */
    wgri_audio_unlock();
    return playing;
}

void wgri_sound_init(void)
{
    wgri_audio_lock(); /* the mixer may be running already */
    if (!wgri_handle_pool_init(&wgr_sound_pool, WGR_HANDLE_KIND_SOUND, "sound", (void **)&wgr_sounds,
                             sizeof(wgri_sound_t), SOUNDS_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS)) {
        log_error("sound: out of memory");
    }
    wgri_audio_unlock();
}

void wgri_sound_deinit(void)
{
    for (uint16_t i = 1; i < wgr_sound_pool.capacity; i++) {
        if (wgr_sound_pool.occupied[i]) {
            wgri_audio_lock();
            wgri_audio_stream_free(&wgr_sounds[i]);
            wgr_audio_release(wgr_sounds[i].audio);
            wgr_sounds[i] = (wgri_sound_t){0};
            wgri_audio_unlock();
        }
    }
    wgri_audio_lock();
    wgri_handle_pool_destroy(&wgr_sound_pool);
    wgri_audio_unlock();
}

int wgri_sound_slot_count(void)
{
    return wgr_sound_pool.capacity;
}

wgri_sound_t *wgri_sound_slot(int index)
{
    return index > 0 && index < wgr_sound_pool.capacity && wgr_sound_pool.occupied[index] ? &wgr_sounds[index] : NULL;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgri_module.h). */
static wgri_module_t wgr_sound_module = {.name = "sound", .order = 91, .init = wgri_sound_init, .deinit = wgri_sound_deinit};
WGRI_MODULE(wgr_sound_module)
