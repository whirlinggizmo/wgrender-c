#include "rl_music.h"

#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>

#include "internal/exports.h"
#include "internal/rl_handle_pool.h"
#include "logger/logger.h"
#include "path/path.h"

#define MAX_MUSIC 128

typedef struct
{
    bool in_use;
    Music music;
    unsigned char *data;
    int data_size;
    unsigned int ref_count;
    bool loop;
    char path[256];
} rl_music_entry_t;

static rl_music_entry_t rl_music_entries[MAX_MUSIC];
static rl_handle_pool_t rl_music_pool;
static uint16_t rl_music_free_indices[MAX_MUSIC];
static uint16_t rl_music_generations[MAX_MUSIC];
static unsigned char rl_music_occupied[MAX_MUSIC];

static rl_music_entry_t *resolve_music_entry(rl_handle_t handle)
{
    uint16_t index = 0;
    if (!rl_handle_pool_resolve(&rl_music_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid music handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_music_entries[index].in_use) {
        log_warn("Stale music handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_music_entries[index];
}

static rl_handle_t find_music_by_path(const char *normalized_path)
{
    if (!normalized_path || normalized_path[0] == '\0') {
        return 0;
    }

    for (uint16_t i = 1; i < MAX_MUSIC; i++) {
        if (!rl_music_entries[i].in_use) {
            continue;
        }
        if (strcmp(rl_music_entries[i].path, normalized_path) == 0) {
            return rl_handle_pool_handle_from_index(&rl_music_pool, i);
        }
    }

    return 0;
}

static void reset_music_entry(rl_music_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    entry->in_use = false;
    entry->music = (Music){0};
    entry->data = NULL;
    entry->data_size = 0;
    entry->ref_count = 0;
    entry->loop = false;
    entry->path[0] = '\0';
}

static bool ensure_music_audio_device(void)
{
    if (IsAudioDeviceReady()) {
        return true;
    }

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        log_error("Failed to initialize audio device");
        return false;
    }

    return true;
}

static const char *extension_for_music_path(const char *path)
{
    const char *ext = path_get_extension(path);
    if (ext && ext[0] != '\0') {
        static char with_dot[32];
        snprintf(with_dot, sizeof(with_dot), ".%s", ext);
        return with_dot;
    }
    return ".mp3";
}

RL_KEEP
rl_handle_t rl_music_create(const char *filename)
{
    char normalized_path[256] = {0};
    rl_handle_t handle = 0;
    uint16_t index = 0;
    rl_music_entry_t *entry = NULL;
    unsigned char *music_data = NULL;
    int music_data_size = 0;
    const char *music_ext = NULL;
    Music loaded_music = {0};

    if (!filename || filename[0] == '\0') {
        return 0;
    }

    if (!ensure_music_audio_device()) {
        return 0;
    }

    path_normalize(filename, normalized_path, sizeof(normalized_path));
    handle = find_music_by_path(normalized_path);
    if (handle != 0) {
        entry = resolve_music_entry(handle);
        if (entry != NULL) {
            entry->ref_count++;
        }
        return handle;
    }

    music_ext = extension_for_music_path(normalized_path);

    music_data = LoadFileData(normalized_path, &music_data_size);
    if (music_data == NULL || music_data_size <= 0) {
        log_error("Failed to load music data (%s)", normalized_path);
        return 0;
    }

    loaded_music = LoadMusicStreamFromMemory(music_ext, music_data, music_data_size);
    if (!IsMusicValid(loaded_music)) {
        log_error("Failed to load music stream (%s)", normalized_path);
        UnloadFileData(music_data);
        return 0;
    }

    handle = rl_handle_pool_alloc(&rl_music_pool);
    if (handle == 0) {
        log_error("MAX_MUSIC reached (%d)", MAX_MUSIC);
        UnloadMusicStream(loaded_music);
        UnloadFileData(music_data);
        return 0;
    }
    rl_handle_pool_resolve(&rl_music_pool, handle, &index);

    entry = &rl_music_entries[index];
    reset_music_entry(entry);
    entry->in_use = true;
    entry->music = loaded_music;
    entry->data = music_data;
    entry->data_size = music_data_size;
    entry->ref_count = 1;
    entry->loop = false;
    snprintf(entry->path, sizeof(entry->path), "%s", normalized_path);
    return handle;
}

RL_KEEP
void rl_music_destroy(rl_handle_t handle)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return;
    }

    if (entry->ref_count > 0) {
        entry->ref_count--;
    }

    if (entry->ref_count > 0) {
        return;
    }

    StopMusicStream(entry->music);
    UnloadMusicStream(entry->music);
    if (entry->data != NULL) {
        UnloadFileData(entry->data);
        entry->data = NULL;
    }

    reset_music_entry(entry);
    rl_handle_pool_free(&rl_music_pool, handle);
}

RL_KEEP
bool rl_music_play(rl_handle_t handle)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return false;
    }
    PlayMusicStream(entry->music);
    return true;
}

RL_KEEP
bool rl_music_pause(rl_handle_t handle)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return false;
    }
    PauseMusicStream(entry->music);
    return true;
}

RL_KEEP
bool rl_music_stop(rl_handle_t handle)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return false;
    }
    StopMusicStream(entry->music);
    return true;
}

RL_KEEP
bool rl_music_set_loop(rl_handle_t handle, bool loop)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return false;
    }
    entry->music.looping = loop;
    entry->loop = loop;
    return true;
}

RL_KEEP
bool rl_music_set_volume(rl_handle_t handle, float volume)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return false;
    }
    SetMusicVolume(entry->music, volume);
    return true;
}

RL_KEEP
bool rl_music_is_playing(rl_handle_t handle)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return false;
    }
    return IsMusicStreamPlaying(entry->music);
}

RL_KEEP
bool rl_music_update(rl_handle_t handle)
{
    rl_music_entry_t *entry = resolve_music_entry(handle);
    if (entry == NULL) {
        return false;
    }
    UpdateMusicStream(entry->music);
    return true;
}

RL_KEEP
void rl_music_update_all(void)
{
    for (uint16_t i = 1; i < MAX_MUSIC; i++) {
        if (!rl_music_entries[i].in_use) {
            continue;
        }
        UpdateMusicStream(rl_music_entries[i].music);
    }
}

void rl_music_init(void)
{
    rl_handle_pool_init(&rl_music_pool,
                        RL_HANDLE_KIND_MUSIC,
                        MAX_MUSIC,
                        rl_music_free_indices,
                        MAX_MUSIC,
                        rl_music_generations,
                        rl_music_occupied);
    for (int i = 0; i < MAX_MUSIC; i++) {
        reset_music_entry(&rl_music_entries[i]);
    }
}

void rl_music_deinit(void)
{
    int unloaded = 0;
    for (uint16_t i = 1; i < MAX_MUSIC; i++) {
        rl_handle_t handle = 0;
        if (!rl_music_entries[i].in_use) {
            continue;
        }
        handle = rl_handle_pool_handle_from_index(&rl_music_pool, i);
        if (handle != 0) {
            rl_music_destroy(handle);
            unloaded++;
        }
    }
    rl_handle_pool_reset(&rl_music_pool);

    if (unloaded > 0) {
        log_info("rl_music_deinit: Freed %d music streams", unloaded);
    }
}
