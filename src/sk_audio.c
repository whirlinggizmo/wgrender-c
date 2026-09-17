#include "sk_audio.h"
#include "internal/sk_audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_loader.h"
#include "internal/sk_internal.h"
#include "sk_handle.h"
#include "sk_logger.h"

#if defined(SK_HEADLESS)
/* No audio device in headless builds: nothing calls the mixer except tests. */
#else
#include "sokol_audio.h"
#endif

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
#define STREAM_CHUNK_FRAMES 4096 /* frames decoded at a time while streaming */

/* Mixing model (docs/PLAN-audio.md)
 * ---------------------------------
 * sokol_audio calls the mixer from the audio device's thread (on web, from the
 * browser's audio callback). It reads registered Sounds and their Audio, so every
 * change to those happens under sk_audio_lock. Audio is either decoded (PCM held
 * in memory; files up to SK_AUDIO_STREAM_MIN_BYTES) or streamed (the encoded file
 * held in memory, decoded while playing by a per-Sound decoder). Both play through
 * the same frame-lookup, so they sound identical. */

typedef enum {
    FORMAT_UNKNOWN = 0,
    FORMAT_MP3,
    FORMAT_OGG,
    FORMAT_WAV,
} audio_format_t;

/* Audio resource: shared, refcounted, deduped by source path. */
typedef struct {
    float *pcm;              /* decoded: interleaved float samples */
    unsigned char *encoded;  /* streamed: the whole file */
    int encoded_size;
    audio_format_t format;
    uint64_t frame_count;    /* frames (samples per channel), known for both kinds */
    int channels;
    int sample_rate;
    int ref_count;
    char path[256];
    bool has_path;
} sk_audio_t;

struct sk_audio_stream {
    sk_handle_t audio;  /* the Audio this decoder reads (reopened when it changes) */
    audio_format_t format;
    int channels;
    drmp3 mp3;
    drwav wav;
    stb_vorbis *vorbis;
    float *buffer;      /* STREAM_CHUNK_FRAMES frames, interleaved */
    uint64_t start;     /* frame index of buffer[0] */
    int count;          /* frames in the buffer */
    uint64_t next;      /* frame the decoder reads next */
};

static sk_audio_t sk_audios[MAX_AUDIO];
static sk_handle_pool_t sk_audio_pool;
static uint16_t sk_audio_free_indices[MAX_AUDIO];
static uint16_t sk_audio_generations[MAX_AUDIO];
static unsigned char sk_audio_occupied[MAX_AUDIO];

static sk_sound_t *sk_active_sounds[SK_MAX_ACTIVE_SOUNDS]; /* registered with the mixer */
static int sk_active_count;

/* ----------------------------------------------------------------- lock ---- */

#if defined(_WIN32)
static CRITICAL_SECTION sk_audio_mutex; /* recursive */
static void lock_init(void) { InitializeCriticalSection(&sk_audio_mutex); }
static void lock_destroy(void) { DeleteCriticalSection(&sk_audio_mutex); }
void sk_audio_lock(void) { EnterCriticalSection(&sk_audio_mutex); }
void sk_audio_unlock(void) { LeaveCriticalSection(&sk_audio_mutex); }
#else
static pthread_mutex_t sk_audio_mutex;
static void lock_init(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); /* API calls nest (sound -> audio release) */
    pthread_mutex_init(&sk_audio_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}
static void lock_destroy(void) { pthread_mutex_destroy(&sk_audio_mutex); }
void sk_audio_lock(void) { pthread_mutex_lock(&sk_audio_mutex); }
void sk_audio_unlock(void) { pthread_mutex_unlock(&sk_audio_mutex); }
#endif

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
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != suffix[i]) return false;
    }
    return true;
}

/* Identify the format and read channels, rate and length without decoding. */
static audio_format_t probe(const unsigned char *data, int size, const char *hint, sk_audio_t *out)
{
    const audio_format_t order[3] = {
        ends_with(hint, ".ogg") ? FORMAT_OGG : ends_with(hint, ".wav") ? FORMAT_WAV : FORMAT_MP3,
        ends_with(hint, ".ogg") ? FORMAT_MP3 : FORMAT_OGG,
        ends_with(hint, ".wav") ? FORMAT_MP3 : FORMAT_WAV,
    };
    for (int i = 0; i < 3; i++) {
        if (order[i] == FORMAT_MP3) {
            drmp3 mp3;
            if (drmp3_init_memory(&mp3, data, (size_t)size, NULL)) {
                out->channels = (int)mp3.channels;
                out->sample_rate = (int)mp3.sampleRate;
                out->frame_count = drmp3_get_pcm_frame_count(&mp3); /* scans headers: ~1 ms for 5 minutes */
                drmp3_uninit(&mp3);
                if (out->frame_count > 0) return FORMAT_MP3;
            }
        } else if (order[i] == FORMAT_OGG) {
            int error = 0;
            stb_vorbis *vorbis = stb_vorbis_open_memory(data, size, &error, NULL);
            if (vorbis != NULL) {
                const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
                out->channels = info.channels;
                out->sample_rate = (int)info.sample_rate;
                out->frame_count = stb_vorbis_stream_length_in_samples(vorbis);
                stb_vorbis_close(vorbis);
                return FORMAT_OGG;
            }
        } else {
            drwav wav;
            if (drwav_init_memory(&wav, data, (size_t)size, NULL)) {
                out->channels = (int)wav.channels;
                out->sample_rate = (int)wav.sampleRate;
                out->frame_count = wav.totalPCMFrameCount;
                drwav_uninit(&wav);
                return FORMAT_WAV;
            }
        }
    }
    return FORMAT_UNKNOWN;
}

static bool stream_open(sk_audio_stream_t *stream, const sk_audio_t *audio)
{
    bool ok = false;
    switch (audio->format) {
        case FORMAT_MP3: ok = drmp3_init_memory(&stream->mp3, audio->encoded, (size_t)audio->encoded_size, NULL); break;
        case FORMAT_WAV: ok = drwav_init_memory(&stream->wav, audio->encoded, (size_t)audio->encoded_size, NULL); break;
        case FORMAT_OGG: {
            int error = 0;
            stream->vorbis = stb_vorbis_open_memory(audio->encoded, audio->encoded_size, &error, NULL);
            ok = stream->vorbis != NULL;
            break;
        }
        default: break;
    }
    stream->format = ok ? audio->format : FORMAT_UNKNOWN;
    stream->channels = audio->channels;
    stream->start = 0;
    stream->count = 0;
    stream->next = 0;
    return ok;
}

static void stream_close(sk_audio_stream_t *stream)
{
    switch (stream->format) {
        case FORMAT_MP3: drmp3_uninit(&stream->mp3); break;
        case FORMAT_WAV: drwav_uninit(&stream->wav); break;
        case FORMAT_OGG: stb_vorbis_close(stream->vorbis); break;
        default: break;
    }
    stream->format = FORMAT_UNKNOWN;
    stream->vorbis = NULL;
}

static bool stream_seek(sk_audio_stream_t *stream, uint64_t frame)
{
    bool ok = false;
    switch (stream->format) {
        case FORMAT_MP3: ok = drmp3_seek_to_pcm_frame(&stream->mp3, frame); break;
        case FORMAT_WAV: ok = drwav_seek_to_pcm_frame(&stream->wav, frame); break;
        case FORMAT_OGG: ok = stb_vorbis_seek(stream->vorbis, (unsigned int)frame) != 0; break;
        default: break;
    }
    stream->next = frame;
    stream->start = frame;
    stream->count = 0;
    return ok;
}

/* Decode the next chunk into the buffer. */
static void stream_fill(sk_audio_stream_t *stream)
{
    uint64_t got = 0;
    switch (stream->format) {
        case FORMAT_MP3: got = drmp3_read_pcm_frames_f32(&stream->mp3, STREAM_CHUNK_FRAMES, stream->buffer); break;
        case FORMAT_WAV: got = drwav_read_pcm_frames_f32(&stream->wav, STREAM_CHUNK_FRAMES, stream->buffer); break;
        case FORMAT_OGG:
            got = (uint64_t)stb_vorbis_get_samples_float_interleaved(stream->vorbis, stream->channels, stream->buffer,
                                                                    STREAM_CHUNK_FRAMES * stream->channels);
            break;
        default: break;
    }
    stream->start = stream->next;
    stream->count = (int)got;
    stream->next += got;
}

/* Fully decode an audio file into out->pcm (format, channels, rate and length
 * already probed). */
static bool decode_all(const unsigned char *data, int size, sk_audio_t *out)
{
    const size_t samples = (size_t)out->frame_count * (size_t)out->channels;
    uint64_t got = 0;

    out->pcm = (float *)malloc(samples * sizeof(float));
    if (out->pcm == NULL) {
        return false;
    }
    if (out->format == FORMAT_MP3) {
        drmp3 mp3;
        if (drmp3_init_memory(&mp3, data, (size_t)size, NULL)) {
            got = drmp3_read_pcm_frames_f32(&mp3, out->frame_count, out->pcm);
            drmp3_uninit(&mp3);
        }
    } else if (out->format == FORMAT_WAV) {
        drwav wav;
        if (drwav_init_memory(&wav, data, (size_t)size, NULL)) {
            got = drwav_read_pcm_frames_f32(&wav, out->frame_count, out->pcm);
            drwav_uninit(&wav);
        }
    } else if (out->format == FORMAT_OGG) {
        int error = 0;
        stb_vorbis *vorbis = stb_vorbis_open_memory(data, size, &error, NULL);
        if (vorbis != NULL) {
            got = (uint64_t)stb_vorbis_get_samples_float_interleaved(vorbis, out->channels, out->pcm, (int)samples);
            stb_vorbis_close(vorbis);
        }
    }
    out->frame_count = got; /* what actually decoded */
    return got > 0;
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
        if (sk_audio_occupied[i] && sk_audios[i].has_path && strcmp(sk_audios[i].path, path) == 0) {
            return sk_handle_pool_handle_from_index(&sk_audio_pool, i);
        }
    }
    return 0;
}

static void free_audio_data(sk_audio_t *audio_ptr)
{
    free(audio_ptr->pcm);
    free(audio_ptr->encoded);
}

/* The CPU half of loading audio (any thread): a probed file, decoded or kept for
 * streaming. */
static void *prepare_audio_mode(const char *path, sk_audio_mode_t mode)
{
    sk_audio_t *audio = (sk_audio_t *)calloc(1, sizeof(sk_audio_t));
    unsigned char *bytes;
    int size = 0;

    if (audio == NULL) {
        return NULL;
    }
    bytes = read_file(path, &size);
    if (bytes == NULL) {
        log_error("Failed to read audio: %s", path != NULL ? path : "(null)");
        free(audio);
        return NULL;
    }
    audio->format = probe(bytes, size, path, audio);
    if (audio->format == FORMAT_UNKNOWN || audio->channels <= 0 || audio->frame_count == 0) {
        log_error("audio decode failed for %s", path);
        free(bytes);
        free(audio);
        return NULL;
    }
    if (mode == SK_AUDIO_MODE_STREAM || (mode == SK_AUDIO_MODE_AUTO && size > SK_AUDIO_STREAM_MIN_BYTES)) {
        audio->encoded = bytes; /* kept; decoded while playing */
        audio->encoded_size = size;
    } else {
        const bool ok = decode_all(bytes, size, audio);
        free(bytes);
        if (!ok) {
            log_error("audio decode failed for %s", path);
            free(audio->pcm);
            free(audio);
            return NULL;
        }
    }
    return audio;
}

static void *prepare_audio(const char *path)
{
    return prepare_audio_mode(path, SK_AUDIO_MODE_AUTO);
}

static void discard_audio(void *prepared)
{
    if (prepared != NULL) {
        free_audio_data((sk_audio_t *)prepared); /* no-op once finish took the data */
        free(prepared);
    }
}

static sk_loader_step_t finish_audio(void *prepared, const char *path, sk_handle_t *resource)
{
    sk_audio_t *audio = (sk_audio_t *)prepared;
    uint16_t index = 0;

    if (path != NULL) {
        snprintf(audio->path, sizeof(audio->path), "%s", path);
        audio->has_path = path[0] != '\0';
    }
    audio->ref_count = 1; /* the caller's, until sk_audio_release */

    sk_audio_lock();
    *resource = sk_handle_pool_alloc(&sk_audio_pool);
    if (*resource == 0) {
        sk_audio_unlock();
        log_error("MAX_AUDIO reached (%d)", MAX_AUDIO);
        return SK_LOADER_FAILED;
    }
    sk_handle_pool_resolve(&sk_audio_pool, *resource, &index);
    sk_audios[index] = *audio;
    sk_audio_unlock();
    audio->pcm = NULL; /* owned by the resource now */
    audio->encoded = NULL;
    return SK_LOADER_DONE;
}

static sk_handle_t find_audio(const char *path)
{
    sk_handle_t handle;
    sk_audio_lock();
    handle = find_audio_by_path(path);
    if (handle != 0) {
        sk_audio_retain(handle);
    }
    sk_audio_unlock();
    return handle;
}

static void release_audio(sk_handle_t audio)
{
    sk_audio_lock();
    sk_audio_release(audio);
    sk_audio_unlock();
}

static const sk_loader_t sk_audio_loader = {
    .name = "audio",
    .prepare = prepare_audio,
    .finish = finish_audio,
    .discard = discard_audio,
    .find = find_audio,
    .release = release_audio,
};

sk_handle_t sk_audio_create_mode(const char *path, sk_audio_mode_t mode)
{
    sk_handle_t handle = find_audio(path);
    sk_audio_t *prepared;

    if (handle != 0) {
        return handle;
    }
    /* read and decode without the lock: the mixer keeps running meanwhile */
    prepared = (sk_audio_t *)prepare_audio_mode(path, mode);
    if (prepared == NULL) {
        return 0;
    }
    if (finish_audio(prepared, path, &handle) != SK_LOADER_DONE) {
        handle = 0;
    }
    discard_audio(prepared);
    return handle;
}

bool sk_audio_is_streamed(sk_handle_t handle)
{
    bool streamed;
    sk_audio_lock();
    const sk_audio_t *audio_ptr = resolve_audio(handle);
    streamed = audio_ptr != NULL && audio_ptr->encoded != NULL;
    sk_audio_unlock();
    return streamed;
}

SK_KEEP
sk_handle_t sk_audio_create(const char *path)
{
    return sk_audio_create_mode(path, SK_AUDIO_MODE_AUTO);
}

void sk_audio_retain(sk_handle_t handle)
{
    sk_audio_lock();
    sk_audio_t *audio_ptr = resolve_audio(handle);
    if (audio_ptr != NULL) audio_ptr->ref_count++;
    sk_audio_unlock();
}

SK_KEEP
void sk_audio_release(sk_handle_t handle)
{
    sk_audio_lock();
    sk_audio_t *audio_ptr = resolve_audio(handle);
    if (audio_ptr != NULL) {
        if (audio_ptr->ref_count > 0) audio_ptr->ref_count--;
        if (audio_ptr->ref_count == 0) {
            /* no Sound references it, so no stream reads its bytes */
            free_audio_data(audio_ptr);
            memset(audio_ptr, 0, sizeof(*audio_ptr));
            sk_handle_pool_free(&sk_audio_pool, handle);
        }
    }
    sk_audio_unlock();
}

/* -------------------------------------------------------------- mixer ------ */

void sk_audio_register(sk_sound_t *sound)
{
    sk_audio_lock();
    if (sound != NULL && sk_active_count < SK_MAX_ACTIVE_SOUNDS) {
        sk_active_sounds[sk_active_count++] = sound;
    }
    sk_audio_unlock();
}

void sk_audio_unregister(sk_sound_t *sound)
{
    sk_audio_lock();
    for (int i = 0; i < sk_active_count; i++) {
        if (sk_active_sounds[i] == sound) {
            sk_active_sounds[i] = sk_active_sounds[--sk_active_count];
            break;
        }
    }
    sk_audio_unlock();
}

void sk_audio_stream_free(sk_sound_t *sound)
{
    sk_audio_lock();
    if (sound != NULL && sound->stream != NULL) {
        stream_close(sound->stream);
        free(sound->stream->buffer);
        free(sound->stream);
        sound->stream = NULL;
    }
    sk_audio_unlock();
}

/* The sound's decoder for a streamed Audio, (re)opened when needed. */
static sk_audio_stream_t *sound_stream(sk_sound_t *sound, const sk_audio_t *audio)
{
    sk_audio_stream_t *stream = sound->stream;
    if (stream != NULL && stream->audio == sound->audio && stream->format != FORMAT_UNKNOWN) {
        return stream;
    }
    if (stream == NULL) {
        stream = (sk_audio_stream_t *)calloc(1, sizeof(*stream));
        if (stream == NULL) return NULL;
        sound->stream = stream;
    } else {
        stream_close(stream);
        free(stream->buffer);
        stream->buffer = NULL;
    }
    stream->audio = sound->audio;
    stream->buffer = (float *)malloc((size_t)STREAM_CHUNK_FRAMES * (size_t)audio->channels * sizeof(float));
    if (stream->buffer == NULL || !stream_open(stream, audio)) {
        return NULL;
    }
    return stream;
}

/* Samples of source frame `idx` (must be < frame_count). False if unavailable. */
static bool read_frame(sk_sound_t *sound, const sk_audio_t *audio, uint64_t idx, float *l, float *r)
{
    const float *frame;
    const int ch = audio->channels;

    if (audio->pcm != NULL) {
        frame = &audio->pcm[idx * (uint64_t)ch];
    } else {
        sk_audio_stream_t *stream = sound_stream(sound, audio);
        if (stream == NULL) return false;
        if (idx < stream->start || idx >= stream->start + (uint64_t)stream->count) {
            if (idx != stream->next && !stream_seek(stream, idx)) {
                return false;
            }
            stream_fill(stream);
            if (stream->count == 0 || idx < stream->start) {
                return false;
            }
        }
        frame = &stream->buffer[(idx - stream->start) * (uint64_t)ch];
    }
    *l = frame[0];
    *r = ch >= 2 ? frame[1] : frame[0];
    return true;
}

static void mix_sound(sk_sound_t *sound, const sk_audio_t *audio, float *buf, int frames, int out_rate)
{
    const double step = ((double)audio->sample_rate / (double)out_rate) * (double)sound->pitch;
    const double length = (double)audio->frame_count;
    /* balance: centered plays both channels at full volume; panning fades the other side */
    const float gain_left = sound->volume * (sound->pan > 0.0f ? 1.0f - sound->pan : 1.0f);
    const float gain_right = sound->volume * (sound->pan < 0.0f ? 1.0f + sound->pan : 1.0f);

    for (int i = 0; i < frames && sound->playing; i++) {
        float l, r;
        if (sound->pos >= length) {
            if (sound->loop && length > 0.0) {
                sound->pos = fmod(sound->pos, length);
            } else {
                sound->playing = false;
                break;
            }
        }
        if (sound->pos < 0.0 || !read_frame(sound, audio, (uint64_t)sound->pos, &l, &r)) {
            sound->playing = false;
            break;
        }
        buf[i * 2 + 0] += l * gain_left;
        buf[i * 2 + 1] += r * gain_right;
        sound->pos += step;
    }
}

void sk_audio_mix(float *out, int frames, int sample_rate)
{
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    sk_audio_lock();
    for (int i = 0; i < sk_active_count; i++) {
        sk_sound_t *sound = sk_active_sounds[i];
        if (sound == NULL || !sound->playing) {
            continue;
        }
        uint16_t index = 0;
        if (!sk_handle_pool_resolve(&sk_audio_pool, sound->audio, &index)) {
            continue; /* no Audio attached yet */
        }
        mix_sound(sound, &sk_audios[index], out, frames, sample_rate);
    }
    sk_audio_unlock();
}

#if !defined(SK_HEADLESS)
/* The device's sample rate, read once after saudio_setup (guarded by the lock). The
 * callback must not call sokol_audio's API: saudio_shutdown clears its setup state
 * before it stops the device thread, so a buffer requested during shutdown would
 * assert in saudio_sample_rate. 0 until known: the callback writes silence. */
static int sk_audio_device_rate;

/* sokol_audio's device callback (audio thread on desktop). */
static void stream_callback(float *buffer, int num_frames, int num_channels)
{
    int sample_rate;

    sk_audio_lock();
    sample_rate = sk_audio_device_rate;
    sk_audio_unlock();
    if (num_channels == 2 && sample_rate > 0) {
        sk_audio_mix(buffer, num_frames, sample_rate);
    } else {
        memset(buffer, 0, (size_t)num_frames * (size_t)num_channels * sizeof(float));
    }
}
#endif

void sk_audio_init(void)
{
    memset(sk_audios, 0, sizeof(sk_audios));
    sk_active_count = 0;
    lock_init();
    sk_handle_pool_init(&sk_audio_pool, SK_HANDLE_KIND_AUDIO, MAX_AUDIO,
                        sk_audio_free_indices, MAX_AUDIO,
                        sk_audio_generations, sk_audio_occupied);
    sk_asset_register_loader(".wav", &sk_audio_loader);
    sk_asset_register_loader(".ogg", &sk_audio_loader);
    sk_asset_register_loader(".mp3", &sk_audio_loader);
#if defined(SK_HEADLESS)
    log_info("audio: headless build, no playback");
#else
    saudio_setup(&(saudio_desc){
        .num_channels = 2,
        .stream_cb = stream_callback,
        .logger.func = 0,
    });
    if (!saudio_isvalid()) {
        log_warn("audio device unavailable; playback disabled");
    } else {
        sk_audio_lock();
        sk_audio_device_rate = saudio_sample_rate();
        sk_audio_unlock();
    }
#endif
}

void sk_audio_deinit(void)
{
#if !defined(SK_HEADLESS)
    if (saudio_isvalid()) {
        saudio_shutdown(); /* stops the device thread before anything is freed */
    }
#endif
    sk_audio_lock();
    for (int i = 0; i < sk_active_count; i++) {
        sk_audio_stream_free(sk_active_sounds[i]);
    }
    sk_active_count = 0;
    /* free any audio resources still alive (sounds should have released theirs) */
    for (uint16_t i = 1; i < MAX_AUDIO; i++) {
        if (sk_audio_occupied[i]) {
            free_audio_data(&sk_audios[i]);
            sk_audios[i] = (sk_audio_t){0};
        }
    }
    sk_handle_pool_reset(&sk_audio_pool);
    sk_audio_unlock();
    lock_destroy();
}
