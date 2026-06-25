#include "rl_color.h"

#include <raylib.h>
#include <stdbool.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/rl_handle_pool.h"
#include "logger/logger.h"

#define MAX_COLORS 256
#define RL_COLOR_BUILTIN_COUNT 27
#define RL_COLOR_DYNAMIC_START_INDEX (RL_COLOR_BUILTIN_COUNT + 1)

static Color rl_colors[MAX_COLORS];
static rl_handle_pool_t rl_color_pool;
static uint16_t rl_color_free_indices[MAX_COLORS];
static uint16_t rl_color_generations[MAX_COLORS];
static unsigned char rl_color_occupied[MAX_COLORS];

// Color handles intentionally map to lightweight RGBA values.
// Unlike textures/models, colors are tiny value objects and do not use
// refcounted shared-asset semantics.

// predefined colors
const rl_handle_t RL_COLOR_DEFAULT = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 1, 1);
const rl_handle_t RL_COLOR_LIGHTGRAY = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 2, 1);
const rl_handle_t RL_COLOR_GRAY = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 3, 1);
const rl_handle_t RL_COLOR_DARKGRAY = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 4, 1);
const rl_handle_t RL_COLOR_YELLOW = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 5, 1);
const rl_handle_t RL_COLOR_GOLD = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 6, 1);
const rl_handle_t RL_COLOR_ORANGE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 7, 1);
const rl_handle_t RL_COLOR_PINK = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 8, 1);
const rl_handle_t RL_COLOR_RED = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 9, 1);
const rl_handle_t RL_COLOR_MAROON = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 10, 1);
const rl_handle_t RL_COLOR_GREEN = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 11, 1);
const rl_handle_t RL_COLOR_LIME = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 12, 1);
const rl_handle_t RL_COLOR_DARKGREEN = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 13, 1);
const rl_handle_t RL_COLOR_SKYBLUE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 14, 1);
const rl_handle_t RL_COLOR_BLUE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 15, 1);
const rl_handle_t RL_COLOR_DARKBLUE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 16, 1);
const rl_handle_t RL_COLOR_PURPLE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 17, 1);
const rl_handle_t RL_COLOR_VIOLET = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 18, 1);
const rl_handle_t RL_COLOR_DARKPURPLE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 19, 1);
const rl_handle_t RL_COLOR_BEIGE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 20, 1);
const rl_handle_t RL_COLOR_BROWN = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 21, 1);
const rl_handle_t RL_COLOR_DARKBROWN = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 22, 1);
const rl_handle_t RL_COLOR_WHITE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 23, 1);
const rl_handle_t RL_COLOR_BLACK = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 24, 1);
const rl_handle_t RL_COLOR_BLANK = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 25, 1);
const rl_handle_t RL_COLOR_MAGENTA = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 26, 1);
const rl_handle_t RL_COLOR_RAYWHITE = RL_HANDLE_MAKE(RL_HANDLE_KIND_COLOR, 27, 1);

static bool resolve_color_handle_to_index(rl_handle_t handle, uint16_t *index_out)
{
    if (!rl_handle_pool_resolve(&rl_color_pool, handle, index_out)) {
        if (handle != 0) log_warn("Invalid color handle (%u)", (unsigned int)handle);
        return false;
    }
    return true;
}

static bool is_builtin_color_index(uint16_t index)
{
    return index > 0 && index <= RL_COLOR_BUILTIN_COUNT;
}

RL_KEEP
rl_handle_t rl_color_create(int r, int g, int b, int a)
{
    rl_handle_t handle = rl_handle_pool_alloc(&rl_color_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_COLORS reached (%d)", MAX_COLORS);
        return 0;
    }

    rl_handle_pool_resolve(&rl_color_pool, handle, &index);
    rl_colors[index] = (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    return handle;
}

RL_KEEP
void rl_color_destroy(rl_handle_t handle)
{
    uint16_t index = 0;

    if (!resolve_color_handle_to_index(handle, &index)) {
        return;
    }
    if (is_builtin_color_index(index)) {
        log_error("Cannot destroy built-in color handle (%u)", (unsigned int)handle);
        return;
    }

    rl_colors[index] = (Color){0};
    rl_handle_pool_free(&rl_color_pool, handle);
}

void rl_color_set(rl_handle_t handle, int r, int g, int b, int a)
{
    uint16_t index = 0;

    if (!resolve_color_handle_to_index(handle, &index)) {
        return;
    }

    rl_colors[index] = (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}

Color rl_color_get(rl_handle_t handle)
{
    uint16_t index = 0;

    if (handle == 0) return WHITE;
    if (!resolve_color_handle_to_index(handle, &index)) return MAGENTA;

    return rl_colors[index];
}

void rl_color_init(void)
{
    memset(rl_colors, 0, sizeof(rl_colors));

    rl_handle_pool_init(&rl_color_pool,
                        RL_HANDLE_KIND_COLOR,
                        MAX_COLORS,
                        rl_color_free_indices,
                        MAX_COLORS,
                        rl_color_generations,
                        rl_color_occupied);

    for (uint16_t i = 1; i <= RL_COLOR_BUILTIN_COUNT; i++) {
        rl_color_generations[i] = 1;
        rl_color_occupied[i] = 1;
    }
    rl_color_pool.next_index = RL_COLOR_DYNAMIC_START_INDEX;

    rl_color_set(RL_COLOR_DEFAULT, 255, 0, 255, 255);
    rl_color_set(RL_COLOR_LIGHTGRAY, 200, 200, 200, 255);
    rl_color_set(RL_COLOR_GRAY, 130, 130, 130, 255);
    rl_color_set(RL_COLOR_DARKGRAY, 80, 80, 80, 255);
    rl_color_set(RL_COLOR_YELLOW, 255, 255, 0, 255);
    rl_color_set(RL_COLOR_GOLD, 255, 203, 0, 255);
    rl_color_set(RL_COLOR_ORANGE, 255, 161, 0, 255);
    rl_color_set(RL_COLOR_PINK, 255, 109, 194, 255);
    rl_color_set(RL_COLOR_RED, 230, 41, 55, 255);
    rl_color_set(RL_COLOR_MAROON, 190, 33, 45, 255);
    rl_color_set(RL_COLOR_GREEN, 0, 228, 48, 255);
    rl_color_set(RL_COLOR_LIME, 0, 158, 47, 255);
    rl_color_set(RL_COLOR_DARKGREEN, 0, 117, 44, 255);
    rl_color_set(RL_COLOR_SKYBLUE, 102, 191, 255, 255);
    rl_color_set(RL_COLOR_BLUE, 0, 121, 241, 255);
    rl_color_set(RL_COLOR_DARKBLUE, 0, 82, 172, 255);
    rl_color_set(RL_COLOR_PURPLE, 200, 122, 255, 255);
    rl_color_set(RL_COLOR_VIOLET, 135, 60, 190, 255);
    rl_color_set(RL_COLOR_DARKPURPLE, 112, 31, 126, 255);
    rl_color_set(RL_COLOR_BEIGE, 211, 176, 131, 255);
    rl_color_set(RL_COLOR_BROWN, 127, 106, 79, 255);
    rl_color_set(RL_COLOR_DARKBROWN, 76, 63, 47, 255);
    rl_color_set(RL_COLOR_WHITE, 255, 255, 255, 255);
    rl_color_set(RL_COLOR_BLACK, 0, 0, 0, 255);
    rl_color_set(RL_COLOR_BLANK, 0, 0, 0, 0);
    rl_color_set(RL_COLOR_MAGENTA, 255, 0, 255, 255);
    rl_color_set(RL_COLOR_RAYWHITE, 245, 245, 245, 255);
}

void rl_color_deinit(void)
{
    int rl_colors_reset = 0;

    for (uint16_t i = 1; i < MAX_COLORS; i++) {
        if (rl_color_occupied[i]) {
            rl_colors[i] = (Color){0};
            rl_colors_reset++;
        }
    }

    rl_handle_pool_reset(&rl_color_pool);
    log_info("rl_color_deinit: Reset %d colors", rl_colors_reset);
}
