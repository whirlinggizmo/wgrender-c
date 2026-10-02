#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/wgr_math_internal.h"
#include "internal/wgr_audio_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_thread_internal.h"
#include "wgr_audio.h"
#include "wgr_logger.h"
#include "wgr_sound.h"
#include "test.h"
#include "test_assets.h"
#include "test_os.h"
#include "tests.h"

/* Audio loads on create through the asset layer, rooted at the repository (the tests
 * run from its root), so these paths are asset paths too. */
#define MUSIC "examples/assets/" MUSIC_PATH
#define CLICK_PATH "examples/assets/sounds/click_004.ogg"
#define WAV_PATH WGR_TEST_DIR "/audio_test_tone.wav"
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

/* A copy of the file at `from`, under another name in the test directory, into `out`:
 * the same audio under another path, which is another resource. */
static bool alias(const char *from, const char *name, char *out, size_t out_size)
{
    FILE *in = fopen(from, "rb"), *copy;
    unsigned char buffer[65536];
    size_t n;
    bool ok = in != NULL;
    snprintf(out, out_size, WGR_TEST_DIR "/%s", name);
    copy = ok ? fopen(out, "wb") : NULL;
    ok = copy != NULL;
    while (ok && (n = fread(buffer, 1, sizeof(buffer), in)) > 0) ok = fwrite(buffer, 1, n, copy) == n;
    if (in != NULL) fclose(in);
    if (copy != NULL) fclose(copy);
    return ok;
}

/* Mix `blocks` blocks where only `sound` plays, into out (stereo). Other sounds
 * are paused around it, so two sounds can be compared block by block. */
static void mix_alone(wgr_handle_t sound, float *out, int frames, int rate)
{
    wgr_sound_resume(sound);
    wgri_audio_mix(out, frames, rate);
    wgr_sound_pause(sound);
}

/* Decoded and streamed copies of a file play identically, block by block, with
 * loop and pitch, across several loops. */
static void check_stream_matches_decode(const char *path, int frames, float pitch, int rate)
{
    wgr_handle_t decoded = wgri_audio_create_mode(path, WGRI_AUDIO_MODE_DECODE);
    wgr_handle_t streamed = wgri_audio_create_mode(path, WGRI_AUDIO_MODE_STREAM);
    float a[BLOCK * 2], b[BLOCK * 2];
    int mismatched_blocks = 0, silent_blocks = 0;

    /* the path dedupes: the second create returns the first Audio, however it loads */
    CHECK(decoded != 0 && streamed == decoded);
    wgr_resource_release(streamed);
    CHECK(test_assets_run() > 0);
    CHECK(wgr_resource_get_status(decoded) == WGR_RESOURCE_READY);
    CHECK(!wgri_audio_is_streamed(decoded));

    /* the same file under another path, forced to stream */
    char copy[512];
    CHECK(alias(path, "alias_streamed", copy, sizeof(copy)));
    streamed = wgri_audio_create_mode(copy, WGRI_AUDIO_MODE_STREAM);
    CHECK(test_assets_run() > 0);
    CHECK(streamed != 0 && streamed != decoded);
    CHECK(wgri_audio_is_streamed(streamed));

    wgr_handle_t sa = wgr_sound_create(decoded), sb = wgr_sound_create(streamed);
    wgr_resource_release(decoded); /* sounds hold references; audio stays alive while playing */
    wgr_resource_release(streamed);
    for (int i = 0; i < 2; i++) {
        wgr_handle_t s = i == 0 ? sa : sb;
        wgr_sound_set_loop(s, true);
        wgr_sound_set_pitch(s, pitch);
        wgr_sound_play(s);
        wgr_sound_pause(s);
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
    CHECK(wgr_sound_is_playing(sa) == false);   /* paused by mix_alone */
    wgr_sound_destroy(sa);
    wgr_sound_destroy(sb);
    remove(copy);
}

void test_audio_streaming(void)
{
    wgri_audio_init();
    wgri_sound_init();
    test_assets_start(0, ".");
    wgr_logger_set_level(WGR_LOGGER_LEVEL_WARN);

    CHECK(write_test_wav(WAV_PATH, 22050, 22050 * 3 / 2)); /* 1.5 s */
    /* WAV: 1.5 s looping ~3.4 times at pitch 1.37, resampled to 48 kHz */
    check_stream_matches_decode(WAV_PATH, 48000 * 3, 1.37f, 48000);
    /* OGG sound effect: short, loops many times */
    check_stream_matches_decode(CLICK_PATH, 44100 * 2, 0.8f, 44100);
    /* MP3 music: the first 3 seconds, at pitch 2 so blocks cross decode chunks unevenly */
    check_stream_matches_decode(MUSIC, 44100 * 3, 2.0f, 44100);

    /* automatic choice: the music (over 1 MB) streams, the small click decodes */
    wgr_handle_t music = wgr_audio_create(MUSIC), click = wgr_audio_create(CLICK_PATH);
    char reference_copy[512];
    CHECK(alias(MUSIC, "alias_decoded.mp3", reference_copy, sizeof(reference_copy)));
    wgr_handle_t reference_audio = wgri_audio_create_mode(reference_copy, WGRI_AUDIO_MODE_DECODE);
    CHECK(test_assets_run() > 0);
    CHECK(wgri_audio_is_streamed(music));
    CHECK(!wgri_audio_is_streamed(click));

    /* two sounds on one streamed Audio keep independent positions */
    wgr_handle_t first = wgr_sound_create(music), second = wgr_sound_create(music), reference = wgr_sound_create(reference_audio);
    float mixed[BLOCK * 2], expected[BLOCK * 2];
    wgr_sound_play(first);
    for (int i = 0; i < 20; i++) wgri_audio_mix(mixed, BLOCK, 44100); /* first moves ahead */
    wgr_sound_set_volume(first, 0.0f);   /* silent, but still decoding its own position */
    wgr_sound_play(second);              /* from the start */
    wgri_audio_mix(mixed, BLOCK, 44100);
    wgr_sound_pause(first);
    wgr_sound_pause(second);
    wgr_sound_play(reference);
    wgri_audio_mix(expected, BLOCK, 44100);
    CHECK(memcmp(mixed, expected, sizeof(mixed)) == 0);

    /* rewinding a streamed sound (seek back to the start) matches the decoded start */
    wgr_sound_set_volume(first, 1.0f);
    wgr_sound_play(first);
    wgr_sound_pause(reference);
    wgri_audio_mix(mixed, BLOCK, 44100);
    CHECK(memcmp(mixed, expected, sizeof(mixed)) == 0);
    wgr_sound_pause(first);

    /* the Audio outlives its creator's reference while sounds use it */
    wgr_resource_release(music);
    wgr_sound_play(second);
    wgri_audio_mix(mixed, BLOCK, 44100);
    CHECK(wgr_sound_is_playing(second));
    wgr_sound_destroy(first);
    wgr_sound_destroy(second); /* last reference: the Audio is freed */
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the stale handle logs */
    CHECK(!wgri_audio_is_streamed(music));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_WARN);

    /* switching a playing sound to another Audio keeps playing it */
    CHECK(wgri_audio_is_streamed(reference_audio) == false);
    wgr_sound_set_audio(reference, click);
    wgr_sound_set_loop(reference, true); /* the click is shorter than a block */
    wgr_sound_play(reference);
    wgri_audio_mix(mixed, BLOCK, 44100);
    CHECK(wgr_sound_is_playing(reference));

    /* non-looping sounds stop at the end */
    wgr_sound_set_loop(reference, false);
    for (int i = 0; i < 100 && wgr_sound_is_playing(reference); i++) wgri_audio_mix(mixed, BLOCK, 44100);
    CHECK(!wgr_sound_is_playing(reference));

    wgr_sound_destroy(reference);
    wgr_resource_release(reference_audio);
    wgr_resource_release(click);
    remove(WAV_PATH);
    remove(reference_copy);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    test_assets_stop();
    wgri_sound_deinit();
    wgri_audio_deinit();
}

/* A sound playing an Audio still loading waits for it, and plays from its start once
 * it's READY; one whose Audio failed stays silent. */
void test_audio_pending(void)
{
    float out[BLOCK * 2];
    float peak = 0.0f;

    wgri_audio_init();
    wgri_sound_init();
    test_assets_start(0, ".");
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the missing file logs */
    CHECK(write_test_wav(WAV_PATH, 22050, 22050));
    const wgr_handle_t tone = wgr_audio_create(WAV_PATH), missing = wgr_audio_create("no/such.ogg");
    const wgr_handle_t sound = wgr_sound_create(tone), silent = wgr_sound_create(missing);
    CHECK(wgr_resource_get_status(tone) == WGR_RESOURCE_PENDING);
    wgr_sound_play(sound);
    wgr_sound_play(silent);
    wgri_audio_mix(out, BLOCK, 22050);
    for (int i = 0; i < BLOCK * 2; i++) peak = fmaxf(peak, fabsf(out[i]));
    CHECK(peak == 0.0f && wgr_sound_is_playing(sound)); /* waiting, not finished */
    CHECK(test_assets_run() > 0);
    CHECK(wgr_resource_get_status(missing) == WGR_RESOURCE_FAILED);
    wgr_sound_pause(silent);
    wgri_audio_mix(out, BLOCK, 22050);
    for (int i = 0; i < BLOCK * 2; i++) peak = fmaxf(peak, fabsf(out[i]));
    CHECK(peak > 0.1f); /* it plays once loaded */
    wgr_sound_pause(sound);
    wgr_sound_resume(silent);
    peak = 0.0f;
    wgri_audio_mix(out, BLOCK, 22050);
    for (int i = 0; i < BLOCK * 2; i++) peak = fmaxf(peak, fabsf(out[i]));
    CHECK(peak == 0.0f); /* a failed one stays silent */

    wgr_sound_destroy(sound);
    wgr_sound_destroy(silent);
    wgr_resource_release(tone);
    wgr_resource_release(missing);
    remove(WAV_PATH);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    test_assets_stop();
    wgri_sound_deinit();
    wgri_audio_deinit();
}

/* The device thread mixes while the game thread changes sounds and creates and
 * destroys Audio. Checks it doesn't crash or deadlock; run under ThreadSanitizer
 * (the tsan preset) to check the locking. */
static wgri_mutex_t mixer_lock;
static bool mixer_running;

static bool mixer_should_run(void)
{
    wgri_mutex_lock(&mixer_lock);
    const bool running = mixer_running;
    wgri_mutex_unlock(&mixer_lock);
    return running;
}

static void mixer_thread(void *user)
{
    float buffer[512 * 2];
    int *blocks = (int *)user;
    while (mixer_should_run()) {
        wgri_audio_mix(buffer, 512, 44100);
        (*blocks)++;
        test_sleep_ms(1); /* like a device: blocks arrive over time, not in a spin */
    }
}

void test_audio_threads(void)
{
    wgri_thread_t thread;
    int blocks = 0;

    wgri_audio_init();
    wgri_sound_init();
    test_assets_start(1, ".");
    wgr_logger_set_level(WGR_LOGGER_LEVEL_WARN);

    wgr_handle_t music = wgr_audio_create(MUSIC);
    wgr_handle_t click = wgr_audio_create(CLICK_PATH);
    CHECK(test_assets_run() > 0);
    wgr_handle_t sounds[4];
    for (int i = 0; i < 4; i++) {
        sounds[i] = wgr_sound_create(i % 2 ? click : music);
        wgr_sound_set_loop(sounds[i], true);
        wgr_sound_play(sounds[i]);
    }

    wgri_mutex_init(&mixer_lock);
    mixer_running = true;
    CHECK(wgri_thread_create(&thread, mixer_thread, &blocks));
    for (int step = 0; step < 4000; step++) {
        wgr_handle_t s = sounds[step % 4];
        switch (step % 9) {
            case 0: wgr_sound_play(s); break;
            case 1: wgr_sound_set_pitch(s, 0.5f + (float)(step % 7) * 0.25f); break;
            case 2: wgr_sound_set_audio(s, step % 2 ? music : click); break;
            case 3: wgr_sound_pause(s); break;
            case 4: wgr_sound_resume(s); break;
            case 5: (void)wgr_sound_is_playing(s); break;
            case 6: wgr_sound_stop(s); wgr_sound_play(s); break;
            case 7: { /* create and drop a whole Audio + Sound while mixing: the pool grows and
                         shrinks under the mixer, and a release while loading cancels the load */
                wgr_handle_t audio = wgri_audio_create_mode(WGR_TEST_DIR "/never_loaded.ogg", WGRI_AUDIO_MODE_STREAM);
                wgr_handle_t extra = wgr_sound_create(audio);
                wgr_resource_release(audio);
                wgr_sound_play(extra);
                wgr_sound_destroy(extra);
                break;
            }
            case 8:
                if (step % 900 == 8) { /* many sounds at once: the pool grows while mixing */
                    wgr_handle_t batch[200];
                    for (int j = 0; j < 200; j++) {
                        batch[j] = wgr_sound_create(click);
                        wgr_sound_play(batch[j]);
                    }
                    for (int j = 0; j < 200; j++) wgr_sound_destroy(batch[j]);
                    break;
                }
                wgr_sound_set_volume(s, (float)(step % 5) / 4.0f);
                break;
            default: break;
        }
    }
    wgri_mutex_lock(&mixer_lock);
    mixer_running = false;
    wgri_mutex_unlock(&mixer_lock);
    wgri_thread_join(&thread);
    wgri_mutex_destroy(&mixer_lock);
    CHECK(blocks > 0);

    for (int i = 0; i < 4; i++) wgr_sound_destroy(sounds[i]);
    wgr_resource_release(music);
    wgr_resource_release(click);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the cancelled loads' file isn't there */
    CHECK(test_assets_run() > 0);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    test_assets_stop();
    wgri_sound_deinit();
    wgri_audio_deinit();
}

/* Every sound is mixed, however many exist (sounds past the 128th used to be
 * silently left out). */
void test_audio_many_sounds(void)
{
    enum { COUNT = 300 };
    static wgr_handle_t sounds[COUNT];
    float out[BLOCK * 2];
    float peak = 0.0f;

    wgri_audio_init();
    wgri_sound_init();
    test_assets_start(0, ".");
    wgr_logger_set_level(WGR_LOGGER_LEVEL_WARN);
    CHECK(write_test_wav(WAV_PATH, 22050, 22050));
    wgr_handle_t tone = wgr_audio_create(WAV_PATH);
    CHECK(test_assets_run() > 0);
    for (int i = 0; i < COUNT; i++) {
        sounds[i] = wgr_sound_create(i == COUNT - 1 ? tone : 0); /* only the last one has sound */
        CHECK(sounds[i] != 0);
    }
    wgr_sound_play(sounds[COUNT - 1]);
    wgri_audio_mix(out, BLOCK, 22050);
    for (int i = 0; i < BLOCK * 2; i++) peak = fmaxf(peak, fabsf(out[i]));
    CHECK(peak > 0.1f);

    for (int i = 0; i < COUNT; i++) wgr_sound_destroy(sounds[i]);
    wgr_resource_release(tone);
    remove(WAV_PATH);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    test_assets_stop();
    wgri_sound_deinit();
    wgri_audio_deinit();
}
