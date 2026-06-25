#include "internal/sk_audio.h"

#include <stdlib.h>
#include <string.h>

#include "internal/sk_internal.h"
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

#define SK_MAX_VOICES 128
#define SK_MIX_MAX_FRAMES 8192

static sk_voice_t *sk_voices[SK_MAX_VOICES];
static int sk_voice_count;
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

static bool decode_mp3(const unsigned char *data, int size, sk_voice_t *out)
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

static bool decode_wav(const unsigned char *data, int size, sk_voice_t *out)
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

static bool decode_ogg(const unsigned char *data, int size, sk_voice_t *out)
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

bool sk_audio_decode(const unsigned char *data, int size, const char *hint, sk_voice_t *out)
{
    bool ok = false;

    if (data == NULL || size <= 0 || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->volume = 1.0f;
    out->pitch = 1.0f;

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

void sk_voice_free(sk_voice_t *voice)
{
    if (voice != NULL && voice->pcm != NULL) {
        free(voice->pcm);
        voice->pcm = NULL;
        voice->frame_count = 0;
    }
}

/* -------------------------------------------------------------- mixer ------ */

void sk_audio_register(sk_voice_t *voice)
{
    if (voice == NULL || sk_voice_count >= SK_MAX_VOICES) {
        return;
    }
    sk_voices[sk_voice_count++] = voice;
}

void sk_audio_unregister(sk_voice_t *voice)
{
    for (int i = 0; i < sk_voice_count; i++) {
        if (sk_voices[i] == voice) {
            sk_voices[i] = sk_voices[--sk_voice_count];
            return;
        }
    }
}

static void mix_voice(sk_voice_t *v, float *buf, int frames, int out_rate)
{
    double step = ((double)v->sample_rate / (double)out_rate) * (double)v->pitch;
    int ch = v->channels;

    for (int i = 0; i < frames; i++) {
        uint64_t idx;
        float l, r;

        if (!v->playing) {
            break;
        }
        if (v->pos >= (double)v->frame_count) {
            if (v->loop && v->frame_count > 0) {
                v->pos -= (double)v->frame_count;
            } else {
                v->playing = false;
                break;
            }
        }
        idx = (uint64_t)v->pos;
        if (idx >= v->frame_count) {
            v->playing = false;
            break;
        }
        if (ch >= 2) {
            l = v->pcm[idx * ch + 0];
            r = v->pcm[idx * ch + 1];
        } else {
            l = r = v->pcm[idx];
        }
        buf[i * 2 + 0] += l * v->volume;
        buf[i * 2 + 1] += r * v->volume;
        v->pos += step;
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
    for (int i = 0; i < sk_voice_count; i++) {
        sk_voice_t *v = sk_voices[i];
        if (v != NULL && v->playing && v->pcm != NULL) {
            mix_voice(v, sk_mix_buffer, frames, out_rate);
        }
    }
    saudio_push(sk_mix_buffer, frames);
}

void sk_audio_init(void)
{
    sk_voice_count = 0;
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
    sk_voice_count = 0;
}
