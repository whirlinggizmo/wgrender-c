#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "internal/sk_audio.h"
#include "internal/sk_internal.h"
#include "sk_audio.h"
#include "sk_logger.h"
#include "sk_sound.h"
#include "test.h"
#include "tests.h"

#define MUSIC_PATH "../examples/assets/music/ethernight_club.mp3" /* tests run from tests/ */
#define CLICK_PATH "../examples/assets/sounds/click_004.ogg"
#define WAV_PATH "build/audio_test_tone.wav"
#define BLOCK 1024

/* A 16-bit stereo WAV: a rising tone left, a falling tone right. */
static bool write_test_wav(const char *path, int rate, int frames)
{
    FILE *f = fopen(path, "wb");
    const int data_size = frames * 4;
    if (f == NULL) return false;
    fwrite("RIFF", 1, 4, f);
    fwrite(&(int){36 + data_size}, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&(int){16}, 4, 1, f);
    fwrite(&(short){1}, 2, 1, f);   /* PCM */
    fwrite(&(short){2}, 2, 1, f);   /* channels */
    fwrite(&rate, 4, 1, f);
    fwrite(&(int){rate * 4}, 4, 1, f);
    fwrite(&(short){4}, 2, 1, f);
    fwrite(&(short){16}, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    for (int i = 0; i < frames; i++) {
        const double t = (double)i / rate;
        const short l = (short)(12000.0 * sin(2.0 * M_PI * (200.0 + 400.0 * t) * t));
        const short r = (short)(12000.0 * sin(2.0 * M_PI * (900.0 - 300.0 * t) * t));
        fwrite(&l, 2, 1, f);
        fwrite(&r, 2, 1, f);
    }
    fclose(f);
    return true;
}

/* Mix `blocks` blocks where only `sound` plays, into out (stereo). Other sounds
 * are paused around it, so two sounds can be compared block by block. */
static void mix_alone(sk_handle_t sound, float *out, int frames, int rate)
{
    sk_sound_resume(sound);
    sk_audio_mix(out, frames, rate);
    sk_sound_pause(sound);
}

/* Decoded and streamed copies of a file play identically, block by block, with
 * loop and pitch, across several loops. */
static void check_stream_matches_decode(const char *path, int frames, float pitch, int rate)
{
    sk_handle_t decoded = sk_audio_create_mode(path, SK_AUDIO_MODE_DECODE);
    sk_handle_t streamed = sk_audio_create_mode(path, SK_AUDIO_MODE_STREAM);
    float a[BLOCK * 2], b[BLOCK * 2];
    int mismatched_blocks = 0, silent_blocks = 0;

    /* the path dedupes: the second create returns the first Audio */
    CHECK(decoded != 0 && streamed == decoded);
    sk_audio_release(streamed);
    CHECK(!sk_audio_is_streamed(decoded));

    /* the same file under another path, forced to stream */
    char alias[512];
    snprintf(alias, sizeof(alias), "./%s", path);
    streamed = sk_audio_create_mode(alias, SK_AUDIO_MODE_STREAM);
    CHECK(streamed != 0 && streamed != decoded);
    CHECK(sk_audio_is_streamed(streamed));

    sk_handle_t sa = sk_sound_create(decoded), sb = sk_sound_create(streamed);
    sk_audio_release(decoded); /* sounds hold references; audio stays alive while playing */
    sk_audio_release(streamed);
    for (int i = 0; i < 2; i++) {
        sk_handle_t s = i == 0 ? sa : sb;
        sk_sound_set_loop(s, true);
        sk_sound_set_pitch(s, pitch);
        sk_sound_play(s);
        sk_sound_pause(s);
    }
    for (int done = 0; done < frames; done += BLOCK) {
        float peak = 0.0f;
        mix_alone(sa, a, BLOCK, rate);
        mix_alone(sb, b, BLOCK, rate);
        if (memcmp(a, b, sizeof(a)) != 0) mismatched_blocks++;
        for (int k = 0; k < BLOCK * 2; k++) peak = fabsf(a[k]) > peak ? fabsf(a[k]) : peak;
        if (peak == 0.0f) silent_blocks++;
    }
    if (mismatched_blocks != 0) {
        fprintf(stderr, "    %s: %d blocks differ between streamed and decoded\n", path, mismatched_blocks);
    }
    CHECK(mismatched_blocks == 0);
    CHECK(silent_blocks < frames / BLOCK); /* it actually played */
    CHECK(sk_sound_is_playing(sa) == false);   /* paused by mix_alone */
    sk_sound_destroy(sa);
    sk_sound_destroy(sb);
}

void test_audio_streaming(void)
{
    sk_audio_init();
    sk_sound_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_WARN);

    CHECK(write_test_wav(WAV_PATH, 22050, 22050 * 3 / 2)); /* 1.5 s */
    /* WAV: 1.5 s looping ~3.4 times at pitch 1.37, resampled to 48 kHz */
    check_stream_matches_decode(WAV_PATH, 48000 * 3, 1.37f, 48000);
    /* OGG sound effect: short, loops many times */
    check_stream_matches_decode(CLICK_PATH, 44100 * 2, 0.8f, 44100);
    /* MP3 music: the first 3 seconds, at pitch 2 so blocks cross decode chunks unevenly */
    check_stream_matches_decode(MUSIC_PATH, 44100 * 3, 2.0f, 44100);

    /* automatic choice: the 6 MB music streams, the small click decodes */
    sk_handle_t music = sk_audio_create(MUSIC_PATH), click = sk_audio_create(CLICK_PATH);
    CHECK(sk_audio_is_streamed(music));
    CHECK(!sk_audio_is_streamed(click));

    /* two sounds on one streamed Audio keep independent positions */
    sk_handle_t reference_audio = sk_audio_create_mode("./" MUSIC_PATH, SK_AUDIO_MODE_DECODE);
    sk_handle_t first = sk_sound_create(music), second = sk_sound_create(music), reference = sk_sound_create(reference_audio);
    float mixed[BLOCK * 2], expected[BLOCK * 2];
    sk_sound_play(first);
    for (int i = 0; i < 20; i++) sk_audio_mix(mixed, BLOCK, 44100); /* first moves ahead */
    sk_sound_set_volume(first, 0.0f);   /* silent, but still decoding its own position */
    sk_sound_play(second);              /* from the start */
    sk_audio_mix(mixed, BLOCK, 44100);
    sk_sound_pause(first);
    sk_sound_pause(second);
    sk_sound_play(reference);
    sk_audio_mix(expected, BLOCK, 44100);
    CHECK(memcmp(mixed, expected, sizeof(mixed)) == 0);

    /* rewinding a streamed sound (seek back to the start) matches the decoded start */
    sk_sound_set_volume(first, 1.0f);
    sk_sound_play(first);
    sk_sound_pause(reference);
    sk_audio_mix(mixed, BLOCK, 44100);
    CHECK(memcmp(mixed, expected, sizeof(mixed)) == 0);
    sk_sound_pause(first);

    /* the Audio outlives its creator's reference while sounds use it */
    sk_audio_release(music);
    sk_sound_play(second);
    sk_audio_mix(mixed, BLOCK, 44100);
    CHECK(sk_sound_is_playing(second));
    sk_sound_destroy(first);
    sk_sound_destroy(second); /* last reference: the Audio is freed */
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* the stale handle logs */
    CHECK(!sk_audio_is_streamed(music));
    sk_logger_set_level(SK_LOGGER_LEVEL_WARN);

    /* switching a playing sound to another Audio keeps playing it */
    CHECK(sk_audio_is_streamed(reference_audio) == false);
    sk_sound_set_audio(reference, click);
    sk_sound_set_loop(reference, true); /* the click is shorter than a block */
    sk_sound_play(reference);
    sk_audio_mix(mixed, BLOCK, 44100);
    CHECK(sk_sound_is_playing(reference));

    /* non-looping sounds stop at the end */
    sk_sound_set_loop(reference, false);
    for (int i = 0; i < 100 && sk_sound_is_playing(reference); i++) sk_audio_mix(mixed, BLOCK, 44100);
    CHECK(!sk_sound_is_playing(reference));

    sk_sound_destroy(reference);
    sk_audio_release(reference_audio);
    sk_audio_release(click);
    remove(WAV_PATH);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_sound_deinit();
    sk_audio_deinit();
}

/* The device thread mixes while the game thread changes sounds and creates and
 * destroys Audio. Checks it doesn't crash or deadlock; run under ThreadSanitizer
 * (SANITIZE=thread) to check the locking. */
static atomic_int mixer_running;

static void *mixer_thread(void *user)
{
    float buffer[512 * 2];
    int *blocks = (int *)user;
    while (mixer_running) {
        const struct timespec pause = {0, 1000000}; /* like a device: blocks arrive over time, not in a spin */
        sk_audio_mix(buffer, 512, 44100);
        (*blocks)++;
        nanosleep(&pause, NULL);
    }
    return NULL;
}

void test_audio_threads(void)
{
    pthread_t thread;
    int blocks = 0;

    sk_audio_init();
    sk_sound_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_WARN);

    sk_handle_t music = sk_audio_create(MUSIC_PATH);
    sk_handle_t click = sk_audio_create(CLICK_PATH);
    sk_handle_t sounds[4];
    for (int i = 0; i < 4; i++) {
        sounds[i] = sk_sound_create(i % 2 ? click : music);
        sk_sound_set_loop(sounds[i], true);
        sk_sound_play(sounds[i]);
    }

    mixer_running = 1;
    CHECK(pthread_create(&thread, NULL, mixer_thread, &blocks) == 0);
    for (int step = 0; step < 4000; step++) {
        sk_handle_t s = sounds[step % 4];
        switch (step % 9) {
            case 0: sk_sound_play(s); break;
            case 1: sk_sound_set_pitch(s, 0.5f + (float)(step % 7) * 0.25f); break;
            case 2: sk_sound_set_audio(s, step % 2 ? music : click); break;
            case 3: sk_sound_pause(s); break;
            case 4: sk_sound_resume(s); break;
            case 5: (void)sk_sound_is_playing(s); break;
            case 6: sk_sound_stop(s); sk_sound_play(s); break;
            case 7: { /* create and drop a whole Audio + Sound while mixing */
                sk_handle_t audio = sk_audio_create_mode("./" CLICK_PATH, SK_AUDIO_MODE_STREAM);
                sk_handle_t extra = sk_sound_create(audio);
                sk_audio_release(audio);
                sk_sound_play(extra);
                sk_sound_destroy(extra);
                break;
            }
            case 8:
                if (step % 900 == 8) { /* many sounds at once: the pool grows while mixing */
                    sk_handle_t batch[200];
                    for (int j = 0; j < 200; j++) {
                        batch[j] = sk_sound_create(click);
                        sk_sound_play(batch[j]);
                    }
                    for (int j = 0; j < 200; j++) sk_sound_destroy(batch[j]);
                    break;
                }
                sk_sound_set_volume(s, (float)(step % 5) / 4.0f);
                break;
            default: break;
        }
    }
    mixer_running = 0;
    pthread_join(thread, NULL);
    CHECK(blocks > 0);

    for (int i = 0; i < 4; i++) sk_sound_destroy(sounds[i]);
    sk_audio_release(music);
    sk_audio_release(click);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_sound_deinit();
    sk_audio_deinit();
}

/* Every sound is mixed, however many exist (sounds past the 128th used to be
 * silently left out). */
void test_audio_many_sounds(void)
{
    enum { COUNT = 300 };
    static sk_handle_t sounds[COUNT];
    float out[BLOCK * 2];
    float peak = 0.0f;

    sk_audio_init();
    sk_sound_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_WARN);
    CHECK(write_test_wav(WAV_PATH, 22050, 22050));
    sk_handle_t tone = sk_audio_create(WAV_PATH);
    for (int i = 0; i < COUNT; i++) {
        sounds[i] = sk_sound_create(i == COUNT - 1 ? tone : 0); /* only the last one has sound */
        CHECK(sounds[i] != 0);
    }
    sk_sound_play(sounds[COUNT - 1]);
    sk_audio_mix(out, BLOCK, 22050);
    for (int i = 0; i < BLOCK * 2; i++) peak = fmaxf(peak, fabsf(out[i]));
    CHECK(peak > 0.1f);

    for (int i = 0; i < COUNT; i++) sk_sound_destroy(sounds[i]);
    sk_audio_release(tone);
    remove(WAV_PATH);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_sound_deinit();
    sk_audio_deinit();
}
