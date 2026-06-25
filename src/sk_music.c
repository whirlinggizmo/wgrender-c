#include "sk_music.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_audio.h"
#include "internal/sk_handle_pool.h"
#include "sk_logger.h"

#define MAX_MUSIC 32

static sk_voice_t sk_music_voices[MAX_MUSIC];
static sk_handle_pool_t sk_music_pool;
static uint16_t sk_music_free_indices[MAX_MUSIC];
static uint16_t sk_music_generations[MAX_MUSIC];
static unsigned char sk_music_occupied[MAX_MUSIC];

static sk_voice_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_music_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid music handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_music_voices[index];
}

static unsigned char *read_file(const char *path, int *out_size)
{
    FILE *f;
    long size;
    unsigned char *bytes;

    f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return NULL; }
    bytes = (unsigned char *)malloc((size_t)size);
    if (bytes == NULL) { fclose(f); return NULL; }
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (int)size;
    return bytes;
}

SK_KEEP
sk_handle_t sk_music_create_from_memory(const unsigned char *data, int size, const char *hint)
{
    sk_handle_t handle;
    uint16_t index = 0;
    sk_voice_t voice;

    if (!sk_audio_decode(data, size, hint, &voice)) {
        return 0;
    }
    handle = sk_handle_pool_alloc(&sk_music_pool);
    if (handle == 0) {
        sk_voice_free(&voice);
        log_error("MAX_MUSIC reached (%d)", MAX_MUSIC);
        return 0;
    }
    sk_handle_pool_resolve(&sk_music_pool, handle, &index);
    voice.loop = true; /* music loops by default */
    voice.playing = false;
    sk_music_voices[index] = voice;
    sk_audio_register(&sk_music_voices[index]);
    return handle;
}

SK_KEEP
sk_handle_t sk_music_create(const char *path)
{
    int size = 0;
    unsigned char *bytes = read_file(path, &size);
    sk_handle_t handle;
    if (bytes == NULL) {
        log_error("Failed to read music: %s", path ? path : "(null)");
        return 0;
    }
    handle = sk_music_create_from_memory(bytes, size, path);
    free(bytes);
    return handle;
}

SK_KEEP
void sk_music_destroy(sk_handle_t handle)
{
    sk_voice_t *v = resolve(handle);
    if (v == NULL) return;
    sk_audio_unregister(v);
    sk_voice_free(v);
    memset(v, 0, sizeof(*v));
    sk_handle_pool_free(&sk_music_pool, handle);
}

SK_KEEP
bool sk_music_play(sk_handle_t handle)
{
    sk_voice_t *v = resolve(handle);
    if (v == NULL) return false;
    v->playing = true; /* resume from current position */
    return true;
}

SK_KEEP
bool sk_music_pause(sk_handle_t handle)
{
    sk_voice_t *v = resolve(handle);
    if (v == NULL) return false;
    v->playing = false;
    return true;
}

SK_KEEP
bool sk_music_stop(sk_handle_t handle)
{
    sk_voice_t *v = resolve(handle);
    if (v == NULL) return false;
    v->playing = false;
    v->pos = 0.0;
    return true;
}

SK_KEEP
bool sk_music_set_loop(sk_handle_t handle, bool loop)
{
    sk_voice_t *v = resolve(handle);
    if (v == NULL) return false;
    v->loop = loop;
    return true;
}

SK_KEEP
bool sk_music_set_volume(sk_handle_t handle, float volume)
{
    sk_voice_t *v = resolve(handle);
    if (v == NULL) return false;
    v->volume = volume;
    return true;
}

SK_KEEP
bool sk_music_is_playing(sk_handle_t handle)
{
    sk_voice_t *v = resolve(handle);
    return v != NULL && v->playing;
}

void sk_music_init(void)
{
    memset(sk_music_voices, 0, sizeof(sk_music_voices));
    sk_handle_pool_init(&sk_music_pool, SK_HANDLE_KIND_MUSIC, MAX_MUSIC,
                        sk_music_free_indices, MAX_MUSIC,
                        sk_music_generations, sk_music_occupied);
}

void sk_music_deinit(void)
{
    for (uint16_t i = 1; i < MAX_MUSIC; i++) {
        if (sk_music_occupied[i]) {
            sk_audio_unregister(&sk_music_voices[i]);
            sk_voice_free(&sk_music_voices[i]);
        }
    }
    sk_handle_pool_reset(&sk_music_pool);
}
