#include "rl_font.h"

#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/rl_handle_pool.h"
#include "logger/logger.h"
#include "path/path.h"

#define MAX_FONTS 255
#define RL_FONT_DEFAULT_INDEX 1u

typedef struct
{
    bool in_use;
    Font font;
    unsigned int ref_count;
    int size_key;
    char path[256];
} rl_font_entry_t;

static rl_font_entry_t rl_fonts[MAX_FONTS];
static rl_handle_pool_t rl_font_pool;
static uint16_t rl_font_free_indices[MAX_FONTS];
static uint16_t rl_font_generations[MAX_FONTS];
static unsigned char rl_font_occupied[MAX_FONTS];

const rl_handle_t RL_FONT_DEFAULT = RL_HANDLE_MAKE(RL_HANDLE_KIND_FONT, RL_FONT_DEFAULT_INDEX, 1u);

static int cache_key_for_font_size(int font_size)
{
    return font_size;
}

static rl_font_entry_t *resolve_font_entry(rl_handle_t handle)
{
    uint16_t index = 0;

    if (!rl_handle_pool_resolve(&rl_font_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid font handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_fonts[index].in_use) {
        log_warn("Stale font handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_fonts[index];
}

static bool resolve_font_handle_to_index(rl_handle_t handle, uint16_t *index_out)
{
    if (index_out == NULL) {
        return false;
    }
    if (!rl_handle_pool_resolve(&rl_font_pool, handle, index_out)) {
        return false;
    }
    return true;
}

static rl_handle_t find_font_by_path(const char *normalized_path, int size_key)
{
    for (uint16_t i = 2; i < MAX_FONTS; i++)
    {
        if (!rl_fonts[i].in_use) {
            continue;
        }
        if ((rl_fonts[i].size_key == size_key) && (strcmp(rl_fonts[i].path, normalized_path) == 0)) {
            return rl_handle_pool_handle_from_index(&rl_font_pool, i);
        }
    }
    return 0;
}

RL_KEEP
rl_handle_t rl_font_create(const char *filename, int fontSize)
{
    int font_data_size = 0;
    unsigned char *font_data = NULL;
    char normalized_path[256] = {0};
    char font_extension[32] = {0};
    int size_key = cache_key_for_font_size(fontSize);
    rl_handle_t handle = 0;
    Font loaded_font = (Font){0};
    uint16_t index = 0;

    if (!filename || filename[0] == '\0') {
        return 0;
    }

    path_normalize(filename, normalized_path, sizeof(normalized_path));

    handle = find_font_by_path(normalized_path, size_key);
    if (handle != 0)
    {
        rl_font_entry_t *entry = resolve_font_entry(handle);
        if (entry != NULL) {
            entry->ref_count++;
        }
        return handle;
    }

    font_data = LoadFileData(normalized_path, &font_data_size);
    if (font_data == NULL || font_data_size <= 0)
    {
        log_error("Failed to load font data (%s)", normalized_path);
        return 0;
    }

    {
        const char *ext = path_get_extension(normalized_path);
        if (ext && ext[0] != '\0') {
            snprintf(font_extension, sizeof(font_extension), ".%s", ext);
        } else {
            snprintf(font_extension, sizeof(font_extension), ".ttf");
        }
    }

    loaded_font = LoadFontFromMemory(font_extension, (const unsigned char *)font_data, font_data_size, fontSize, NULL, 0);
    UnloadFileData(font_data);

    if (!IsFontValid(loaded_font))
    {
        log_error("Failed to load font (%s)", normalized_path);
        return 0;
    }

    handle = rl_handle_pool_alloc(&rl_font_pool);
    if (handle == 0)
    {
        log_error("MAX_FONTS reached (%d)", MAX_FONTS);
        UnloadFont(loaded_font);
        return 0;
    }
    rl_handle_pool_resolve(&rl_font_pool, handle, &index);

    rl_fonts[index].in_use = true;
    rl_fonts[index].font = loaded_font;
    rl_fonts[index].ref_count = 1;
    rl_fonts[index].size_key = size_key;
    snprintf(rl_fonts[index].path, sizeof(rl_fonts[index].path), "%s", normalized_path);

    return handle;
}

RL_KEEP
void rl_font_destroy(rl_handle_t handle)
{
    rl_font_entry_t *entry = resolve_font_entry(handle);
    if (entry == NULL) {
        return;
    }

    if (handle == RL_FONT_DEFAULT) {
        log_error("Cannot destroy default font handle (%u)", (unsigned int)handle);
        return;
    }

    if (entry->ref_count > 0) {
        entry->ref_count--;
    }

    if (entry->ref_count == 0)
    {
        UnloadFont(entry->font);
        entry->font = (Font){0};
        entry->in_use = false;
        entry->size_key = 0;
        entry->path[0] = '\0';
        rl_handle_pool_free(&rl_font_pool, handle);
    }
}

RL_KEEP
rl_handle_t rl_font_get_default(void)
{
    return RL_FONT_DEFAULT;
}

Font rl_font_get(rl_handle_t handle)
{
    rl_font_entry_t *entry = resolve_font_entry(handle);
    if (entry == NULL)
    {
        return rl_fonts[RL_FONT_DEFAULT_INDEX].font;
    }

    return entry->font;
}

void rl_font_set(rl_handle_t handle, Font font)
{
    uint16_t index = 0;
    if (!resolve_font_handle_to_index(handle, &index)) {
        if (handle != 0) log_warn("Invalid font handle (%u)", (unsigned int)handle);
        return;
    }

    if (rl_fonts[index].in_use && (handle != RL_FONT_DEFAULT)) {
        UnloadFont(rl_fonts[index].font);
    }

    rl_fonts[index].in_use = true;
    rl_fonts[index].font = font;
    rl_fonts[index].ref_count = 1;
    rl_fonts[index].size_key = 0;
    rl_fonts[index].path[0] = '\0';
}

void rl_font_init(void)
{
    uint16_t default_index = 0;

    rl_handle_pool_init(&rl_font_pool,
                        RL_HANDLE_KIND_FONT,
                        MAX_FONTS,
                        rl_font_free_indices,
                        MAX_FONTS,
                        rl_font_generations,
                        rl_font_occupied);
    for (int i = 0; i < MAX_FONTS; i++)
    {
        rl_fonts[i].in_use = false;
        rl_fonts[i].font = (Font){0};
        rl_fonts[i].ref_count = 0;
        rl_fonts[i].size_key = 0;
        rl_fonts[i].path[0] = '\0';
    }

    if (!rl_handle_pool_resolve(&rl_font_pool, RL_FONT_DEFAULT, &default_index)) {
        rl_handle_t default_handle = rl_handle_pool_alloc(&rl_font_pool);
        if (default_handle != RL_FONT_DEFAULT ||
            !rl_handle_pool_resolve(&rl_font_pool, default_handle, &default_index)) {
            log_error("Failed to initialize default font handle");
            return;
        }
    }

    rl_font_set(RL_FONT_DEFAULT, GetFontDefault());
}

void rl_font_deinit(void)
{
    int fonts_freed = 0;

    for (uint16_t i = 2; i < MAX_FONTS; i++)
    {
        if (!rl_fonts[i].in_use) {
            continue;
        }
        UnloadFont(rl_fonts[i].font);
        rl_fonts[i].in_use = false;
        rl_fonts[i].font = (Font){0};
        rl_fonts[i].ref_count = 0;
        rl_fonts[i].size_key = 0;
        rl_fonts[i].path[0] = '\0';
        fonts_freed++;
    }

    rl_fonts[RL_FONT_DEFAULT_INDEX].in_use = false;
    rl_fonts[RL_FONT_DEFAULT_INDEX].font = (Font){0};
    rl_fonts[RL_FONT_DEFAULT_INDEX].ref_count = 0;
    rl_fonts[RL_FONT_DEFAULT_INDEX].size_key = 0;
    rl_fonts[RL_FONT_DEFAULT_INDEX].path[0] = '\0';
    rl_handle_pool_reset(&rl_font_pool);

    log_info("rl_font_deinit: Freed %d fonts", fonts_freed);
}
