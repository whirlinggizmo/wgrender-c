#include "wgr_sound.h"

#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_audio_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_module_internal.h"
#include "wgr_logger.h"

#define SOUNDS_INITIAL 32 /* slots to start with; the pool doubles as needed */

static wgr_sound_t *wgr_sounds; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_sound_pool;

static wgr_sound_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_sound_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid sound handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &wgr_sounds[index];
}

/* Create a Sound object that plays `audio` (adds its own reference).
 * `audio` may be 0: create the sound now (set volume/pitch/loop, even play())
 * and attach the Audio later via wgr_sound_set_audio(); the mixer skips a playing
 * sound with no Audio, then picks it up once the resource is set.
 * Sounds are shared with the mixer thread: every change takes wgr_audio_lock. */
static wgr_handle_t create_sound(wgr_handle_t audio, bool loop)
{
    wgr_handle_t handle;
    uint16_t index = 0;

    wgr_audio_lock(); /* the mixer walks the pool, which may grow here */
    handle = wgr_handle_pool_alloc(&wgr_sound_pool);
    if (handle == 0) {
        wgr_audio_unlock();
        log_error("sound: pool full (%u)", (unsigned)wgr_sound_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_sound_pool, handle, &index);
    wgr_sounds[index] = (wgr_sound_t){
        .audio = audio,
        .volume = 1.0f,
        .pitch = 1.0f,
        .loop = loop,
        .playing = false,
    };
    if (audio != 0) wgr_audio_retain(audio); /* the sound's own ref to the shared Audio */
    wgr_audio_unlock();
    return handle;
}

WGR_KEEP
bool wgr_sound_set_audio(wgr_handle_t handle, wgr_handle_t audio)
{
    wgr_sound_t *sound_ptr = resolve(handle);
    if (sound_ptr == NULL) return false;
    wgr_audio_lock();
    if (sound_ptr->audio != audio) {
        if (audio != 0) wgr_audio_retain(audio);
        wgr_audio_stream_free(sound_ptr); /* the decoder belongs to the old Audio */
        if (sound_ptr->audio != 0) wgr_audio_release(sound_ptr->audio);
        sound_ptr->audio = audio;
        /* volume/pitch/loop/playing/pos retained, so playback picks up the new Audio */
    }
    wgr_audio_unlock();
    return true;
}

WGR_KEEP
wgr_handle_t wgr_sound_create(wgr_handle_t audio)
{
    return create_sound(audio, false);
}

WGR_KEEP
void wgr_sound_destroy(wgr_handle_t handle)
{
    wgr_sound_t *sound_ptr = resolve(handle);
    wgr_handle_t audio;
    if (sound_ptr == NULL) return;
    wgr_audio_lock();
    wgr_audio_stream_free(sound_ptr);
    audio = sound_ptr->audio;
    memset(sound_ptr, 0, sizeof(*sound_ptr));
    wgr_handle_pool_free(&wgr_sound_pool, handle);
    wgr_audio_release(audio); /* frees the Audio once its last sound is gone */
    wgr_audio_unlock();
}

/* Apply a change to a sound under the mixer lock. */
#define WITH_SOUND(handle, statement)                                                              \
    do {                                                                                           \
        wgr_sound_t *sound_ptr = resolve(handle);                                                   \
        if (sound_ptr == NULL) return false;                                                       \
        wgr_audio_lock();                                                                           \
        statement;                                                                                 \
        wgr_audio_unlock();                                                                         \
        return true;                                                                               \
    } while (0)

WGR_KEEP
bool wgr_sound_play(wgr_handle_t handle)
{
    WITH_SOUND(handle, { sound_ptr->pos = 0.0; sound_ptr->playing = true; });
}

WGR_KEEP
bool wgr_sound_pause(wgr_handle_t handle)
{
    WITH_SOUND(handle, sound_ptr->playing = false); /* keep position */
}

WGR_KEEP
bool wgr_sound_resume(wgr_handle_t handle)
{
    WITH_SOUND(handle, sound_ptr->playing = true); /* from current position */
}

WGR_KEEP
bool wgr_sound_stop(wgr_handle_t handle)
{
    WITH_SOUND(handle, { sound_ptr->playing = false; sound_ptr->pos = 0.0; });
}

WGR_KEEP
bool wgr_sound_set_loop(wgr_handle_t handle, bool loop)
{
    WITH_SOUND(handle, sound_ptr->loop = loop);
}

WGR_KEEP
bool wgr_sound_set_volume(wgr_handle_t handle, float volume)
{
    WITH_SOUND(handle, sound_ptr->volume = volume);
}

WGR_KEEP
bool wgr_sound_set_pan(wgr_handle_t handle, float pan)
{
    pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    WITH_SOUND(handle, sound_ptr->pan = pan);
}

WGR_KEEP
bool wgr_sound_set_pitch(wgr_handle_t handle, float pitch)
{
    WITH_SOUND(handle, sound_ptr->pitch = pitch);
}

WGR_KEEP
bool wgr_sound_is_playing(wgr_handle_t handle)
{
    wgr_sound_t *sound_ptr = resolve(handle);
    bool playing;
    if (sound_ptr == NULL) return false;
    wgr_audio_lock();
    playing = sound_ptr->playing; /* the mixer clears it when a sound ends */
    wgr_audio_unlock();
    return playing;
}

void wgr_sound_init(void)
{
    wgr_audio_lock(); /* the mixer may be running already */
    if (!wgr_handle_pool_init(&wgr_sound_pool, WGR_HANDLE_KIND_SOUND, "sound", (void **)&wgr_sounds,
                             sizeof(wgr_sound_t), SOUNDS_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("sound: out of memory");
    }
    wgr_audio_unlock();
}

void wgr_sound_deinit(void)
{
    for (uint16_t i = 1; i < wgr_sound_pool.capacity; i++) {
        if (wgr_sound_pool.occupied[i]) {
            wgr_audio_lock();
            wgr_audio_stream_free(&wgr_sounds[i]);
            wgr_audio_release(wgr_sounds[i].audio);
            wgr_sounds[i] = (wgr_sound_t){0};
            wgr_audio_unlock();
        }
    }
    wgr_audio_lock();
    wgr_handle_pool_destroy(&wgr_sound_pool);
    wgr_audio_unlock();
}

int wgr_sound_slot_count(void)
{
    return wgr_sound_pool.capacity;
}

wgr_sound_t *wgr_sound_slot(int index)
{
    return index > 0 && index < wgr_sound_pool.capacity && wgr_sound_pool.occupied[index] ? &wgr_sounds[index] : NULL;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgr_module.h). */
static wgr_module_t wgr_sound_module = {.name = "sound", .order = 91, .init = wgr_sound_init, .deinit = wgr_sound_deinit};
WGR_MODULE(wgr_sound_module)
