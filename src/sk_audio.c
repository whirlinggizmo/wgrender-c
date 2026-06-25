#include "sk_audio.h"
#include "internal/sk_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "sk_handle.h"
#include "sk_logger.h"

#include "sokol_audio.h"

/* decoders */
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c" /* implementation */

#define MAX_AUDIO 256
#define SK_MAX_ACTIVE_SOUNDS 128
#define SK_MIX_MAX_FRAMES 8192

/* Audio resource: decoded PCM, shared/refcounted/deduped by source path. Many
 * Sound objects may reference one Audio. */
typedef struct {
    float *pcm;            /* interleaved float samples */
    uint64_t frame_count;  /* frames (samples per channel) */
    int channels;          /* 1 or 2 */
    int sample_rate;       /* source rate */
    int ref_count;
    char path[256];
    bool has_path;
} sk_audio_t;

static sk_audio_t sk_audios[MAX_AUDIO];
static sk_handle_pool_t sk_audio_pool;
static uint16_t sk_audio_free_indices[MAX_AUDIO];
static uint16_t sk_audio_generations[MAX_AUDIO];
static unsigned char sk_audio_occupied[MAX_AUDIO];

static sk_sound_t *sk_active_sounds[SK_MAX_ACTIVE_SOUNDS]; /* registered with the mixer */
static int sk_active_count;
static float sk_mix_buffer[SK_MIX_MAX_FRAMES * 2];

/* ------------------------------------------------------------- decoding ---- */

static bool ends_with(const char *s, const char *suffix)
{
    size_t ls, lsuf;
    if (s == NULL) return false;
    ls = strlen(s);
    lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    for (size_t i = 0; i < lsuf; i++) {
        char a = s[ls - lsuf + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static bool decode_mp3(const unsigned char *data, int size, sk_audio_t *out)
{
    drmp3 mp3;
    drmp3_uint64 frames;
    float *pcm;

    if (!drmp3_init_memory(&mp3, data, (size_t)size, NULL)) {
        return false;
    }
    frames = drmp3_get_pcm_frame_count(&mp3);
    pcm = (float *)malloc((size_t)frames * mp3.channels * sizeof(float));
    if (pcm == NULL) {
        drmp3_uninit(&mp3);
        return false;
    }
    drmp3_read_pcm_frames_f32(&mp3, frames, pcm);
    out->pcm = pcm;
    out->frame_count = frames;
    out->channels = (int)mp3.channels;
    out->sample_rate = (int)mp3.sampleRate;
    drmp3_uninit(&mp3);
    return true;
}

static bool decode_wav(const unsigned char *data, int size, sk_audio_t *out)
{
    unsigned int channels = 0, rate = 0;
    drwav_uint64 frames = 0;
    float *pcm = drwav_open_memory_and_read_pcm_frames_f32(
        data, (size_t)size, &channels, &rate, &frames, NULL);
    if (pcm == NULL) {
        return false;
    }
    out->pcm = pcm;
    out->frame_count = frames;
    out->channels = (int)channels;
    out->sample_rate = (int)rate;
    return true;
}

static bool decode_ogg(const unsigned char *data, int size, sk_audio_t *out)
{
    int channels = 0, rate = 0;
    short *pcm16 = NULL;
    int frames = stb_vorbis_decode_memory(data, size, &channels, &rate, &pcm16);
    if (frames < 0 || pcm16 == NULL) {
        return false;
    }
    /* convert s16 -> f32 */
    float *pcm = (float *)malloc((size_t)frames * channels * sizeof(float));
    if (pcm == NULL) {
        free(pcm16);
        return false;
    }
    for (size_t i = 0; i < (size_t)frames * channels; i++) {
        pcm[i] = pcm16[i] / 32768.0f;
    }
    free(pcm16);
    out->pcm = pcm;
    out->frame_count = (uint64_t)frames;
    out->channels = channels;
    out->sample_rate = rate;
    return true;
}

/* Decode an in-memory audio file (mp3/ogg/wav, chosen by `hint` extension or
 * content) into the PCM fields of `out`. Returns false on failure. */
static bool decode_audio(const unsigned char *data, int size, const char *hint, sk_audio_t *out)
{
    bool ok;

    if (data == NULL || size <= 0 || out == NULL) {
        return false;
    }

    if (ends_with(hint, ".mp3")) {
        ok = decode_mp3(data, size, out);
    } else if (ends_with(hint, ".ogg")) {
        ok = decode_ogg(data, size, out);
    } else if (ends_with(hint, ".wav")) {
        ok = decode_wav(data, size, out);
    } else {
        /* unknown extension: try in turn */
        ok = decode_mp3(data, size, out) || decode_ogg(data, size, out) ||
             decode_wav(data, size, out);
    }

    if (!ok) {
        log_error("audio decode failed for %s", hint ? hint : "(memory)");
    }
    return ok;
}

static unsigned char *read_file(const char *path, int *out_size)
{
    FILE *f;
    long size;
    unsigned char *bytes;

    *out_size = 0;
    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return NULL; }
    bytes = (unsigned char *)malloc((size_t)size);
    if (bytes == NULL) { fclose(f); return NULL; }
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size) { free(bytes); fclose(f); return NULL; }
    fclose(f);
    *out_size = (int)size;
    return bytes;
}

/* ----------------------------------------------------- audio resources ----- */

static sk_audio_t *resolve_audio(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_audio_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid audio handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_audios[index];
}

static sk_handle_t find_audio_by_path(const char *path)
{
    if (path == NULL || path[0] == '\0') return 0;
    for (uint16_t i = 1; i < MAX_AUDIO; i++) {
        if (sk_audio_occupied[i] && sk_audios[i].has_path &&
            strcmp(sk_audios[i].path, path) == 0) {
            return sk_handle_pool_handle_from_index(&sk_audio_pool, i);
        }
    }
    return 0;
}

static sk_handle_t create_audio(const unsigned char *data, int size, const char *path)
{
    sk_handle_t handle;
    uint16_t index = 0;
    sk_audio_t audio = {0};

    if (!decode_audio(data, size, path, &audio)) {
        return 0;
    }
    handle = sk_handle_pool_alloc(&sk_audio_pool);
    if (handle == 0) {
        log_error("MAX_AUDIO reached (%d)", MAX_AUDIO);
        free(audio.pcm);
        return 0;
    }
    if (path != NULL && path[0] != '\0') {
        size_t n = strlen(path);
        if (n >= sizeof(audio.path)) n = sizeof(audio.path) - 1;
        memcpy(audio.path, path, n);
        audio.path[n] = '\0';
        audio.has_path = true;
    }
    audio.ref_count = 0; /* references are added by sounds and explicit ownership */
    sk_handle_pool_resolve(&sk_audio_pool, handle, &index);
    sk_audios[index] = audio;
    return handle;
}

SK_KEEP
sk_handle_t sk_audio_create(const char *path)
{
    sk_handle_t audio = find_audio_by_path(path);
    unsigned char *bytes;
    int size = 0;

    if (audio != 0) { sk_audio_retain(audio); return audio; }
    bytes = read_file(path, &size);
    if (bytes == NULL) {
        log_error("Failed to read audio: %s", path ? path : "(null)");
        return 0;
    }
    audio = create_audio(bytes, size, path);
    free(bytes);
    if (audio == 0) return 0;
    sk_audio_retain(audio); /* caller owns this reference until sk_audio_destroy */
    return audio;
}

SK_KEEP void sk_audio_destroy(sk_handle_t handle) { sk_audio_release(handle); }

void sk_audio_retain(sk_handle_t handle)
{
    sk_audio_t *audio_ptr = resolve_audio(handle);
    if (audio_ptr != NULL) audio_ptr->ref_count++;
}

void sk_audio_release(sk_handle_t handle)
{
    sk_audio_t *audio_ptr = resolve_audio(handle);
    if (audio_ptr == NULL) return;
    if (audio_ptr->ref_count > 0) audio_ptr->ref_count--;
    if (audio_ptr->ref_count == 0) {
        free(audio_ptr->pcm);
        memset(audio_ptr, 0, sizeof(*audio_ptr));
        sk_handle_pool_free(&sk_audio_pool, handle);
    }
}

/* -------------------------------------------------------------- mixer ------ */

void sk_audio_register(sk_sound_t *sound)
{
    if (sound == NULL || sk_active_count >= SK_MAX_ACTIVE_SOUNDS) {
        return;
    }
    sk_active_sounds[sk_active_count++] = sound;
}

void sk_audio_unregister(sk_sound_t *sound)
{
    for (int i = 0; i < sk_active_count; i++) {
        if (sk_active_sounds[i] == sound) {
            sk_active_sounds[i] = sk_active_sounds[--sk_active_count];
            return;
        }
    }
}

static void mix_sound(sk_sound_t *sound, sk_audio_t *audio, float *buf, int frames, int out_rate)
{
    double step = ((double)audio->sample_rate / (double)out_rate) * (double)sound->pitch;
    int ch = audio->channels;

    for (int i = 0; i < frames; i++) {
        uint64_t idx;
        float l, r;

        if (!sound->playing) {
            break;
        }
        if (sound->pos >= (double)audio->frame_count) {
            if (sound->loop && audio->frame_count > 0) {
                sound->pos -= (double)audio->frame_count;
            } else {
                sound->playing = false;
                break;
            }
        }
        idx = (uint64_t)sound->pos;
        if (idx >= audio->frame_count) {
            sound->playing = false;
            break;
        }
        if (ch >= 2) {
            l = audio->pcm[idx * ch + 0];
            r = audio->pcm[idx * ch + 1];
        } else {
            l = r = audio->pcm[idx];
        }
        buf[i * 2 + 0] += l * sound->volume;
        buf[i * 2 + 1] += r * sound->volume;
        sound->pos += step;
    }
}

void sk_audio_tick(void)
{
    int out_rate, frames;

    if (!saudio_isvalid()) {
        return;
    }
    frames = saudio_expect();
    if (frames <= 0) {
        return;
    }
    if (frames > SK_MIX_MAX_FRAMES) {
        frames = SK_MIX_MAX_FRAMES;
    }
    out_rate = saudio_sample_rate();

    memset(sk_mix_buffer, 0, (size_t)frames * 2 * sizeof(float));
    for (int i = 0; i < sk_active_count; i++) {
        sk_sound_t *sound = sk_active_sounds[i];
        sk_audio_t *audio_ptr = sound ? resolve_audio(sound->audio) : NULL;
        if (sound != NULL && sound->playing && audio_ptr != NULL && audio_ptr->pcm != NULL) {
            mix_sound(sound, audio_ptr, sk_mix_buffer, frames, out_rate);
        }
    }
    saudio_push(sk_mix_buffer, frames);
}

void sk_audio_init(void)
{
    memset(sk_audios, 0, sizeof(sk_audios));
    sk_active_count = 0;
    sk_handle_pool_init(&sk_audio_pool, SK_HANDLE_KIND_AUDIO, MAX_AUDIO,
                        sk_audio_free_indices, MAX_AUDIO,
                        sk_audio_generations, sk_audio_occupied);
    saudio_setup(&(saudio_desc){
        .num_channels = 2,
        .logger.func = 0,
    });
    if (!saudio_isvalid()) {
        log_warn("audio device unavailable; playback disabled");
    }
}

void sk_audio_deinit(void)
{
    if (saudio_isvalid()) {
        saudio_shutdown();
    }
    sk_active_count = 0;
    /* free any audio resources still alive (sounds should have released theirs) */
    for (uint16_t i = 1; i < MAX_AUDIO; i++) {
        if (sk_audio_occupied[i]) {
            free(sk_audios[i].pcm);
            sk_audios[i] = (sk_audio_t){0};
        }
    }
    sk_handle_pool_reset(&sk_audio_pool);
}
