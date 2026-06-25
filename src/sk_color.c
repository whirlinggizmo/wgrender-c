#include "sk_color.h"

#include <stdbool.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "sk_logger.h"

#define MAX_COLORS 256
#define SK_COLOR_BUILTIN_COUNT 27
#define SK_COLOR_DYNAMIC_START_INDEX (SK_COLOR_BUILTIN_COUNT + 1)

/* Colors are stored normalized (0..1) since the sokol_gl / sokol_gfx clear path
 * consumes floats. */
static color_t sk_colors[MAX_COLORS];
static sk_handle_pool_t sk_color_pool;
static uint16_t sk_color_free_indices[MAX_COLORS];
static uint16_t sk_color_generations[MAX_COLORS];
static unsigned char sk_color_occupied[MAX_COLORS];

const sk_handle_t SK_COLOR_DEFAULT = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 1, 1);
const sk_handle_t SK_COLOR_LIGHTGRAY = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 2, 1);
const sk_handle_t SK_COLOR_GRAY = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 3, 1);
const sk_handle_t SK_COLOR_DARKGRAY = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 4, 1);
const sk_handle_t SK_COLOR_YELLOW = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 5, 1);
const sk_handle_t SK_COLOR_GOLD = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 6, 1);
const sk_handle_t SK_COLOR_ORANGE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 7, 1);
const sk_handle_t SK_COLOR_PINK = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 8, 1);
const sk_handle_t SK_COLOR_RED = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 9, 1);
const sk_handle_t SK_COLOR_MAROON = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 10, 1);
const sk_handle_t SK_COLOR_GREEN = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 11, 1);
const sk_handle_t SK_COLOR_LIME = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 12, 1);
const sk_handle_t SK_COLOR_DARKGREEN = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 13, 1);
const sk_handle_t SK_COLOR_SKYBLUE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 14, 1);
const sk_handle_t SK_COLOR_BLUE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 15, 1);
const sk_handle_t SK_COLOR_DARKBLUE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 16, 1);
const sk_handle_t SK_COLOR_PURPLE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 17, 1);
const sk_handle_t SK_COLOR_VIOLET = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 18, 1);
const sk_handle_t SK_COLOR_DARKPURPLE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 19, 1);
const sk_handle_t SK_COLOR_BEIGE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 20, 1);
const sk_handle_t SK_COLOR_BROWN = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 21, 1);
const sk_handle_t SK_COLOR_DARKBROWN = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 22, 1);
const sk_handle_t SK_COLOR_WHITE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 23, 1);
const sk_handle_t SK_COLOR_BLACK = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 24, 1);
const sk_handle_t SK_COLOR_BLANK = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 25, 1);
const sk_handle_t SK_COLOR_MAGENTA = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 26, 1);
const sk_handle_t SK_COLOR_RAYWHITE = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, 27, 1);

static color_t normalize(int r, int g, int b, int a)
{
    return (color_t){(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f};
}

static bool resolve_color_handle_to_index(sk_handle_t handle, uint16_t *index_out)
{
    if (!sk_handle_pool_resolve(&sk_color_pool, handle, index_out)) {
        if (handle != 0) {
            log_warn("Invalid color handle (%u)", (unsigned int)handle);
        }
        return false;
    }
    return true;
}

static bool is_builtin_color_index(uint16_t index)
{
    return index > 0 && index <= SK_COLOR_BUILTIN_COUNT;
}

SK_KEEP
sk_handle_t sk_color_create(int r, int g, int b, int a)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_color_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_COLORS reached (%d)", MAX_COLORS);
        return 0;
    }

    sk_handle_pool_resolve(&sk_color_pool, handle, &index);
    sk_colors[index] = normalize(r, g, b, a);
    return handle;
}

SK_KEEP
void sk_color_destroy(sk_handle_t handle)
{
    uint16_t index = 0;

    if (!resolve_color_handle_to_index(handle, &index)) {
        return;
    }
    if (is_builtin_color_index(index)) {
        log_error("Cannot destroy built-in color handle (%u)", (unsigned int)handle);
        return;
    }

    sk_colors[index] = (color_t){0};
    sk_handle_pool_free(&sk_color_pool, handle);
}

void sk_color_set(sk_handle_t handle, int r, int g, int b, int a)
{
    uint16_t index = 0;

    if (!resolve_color_handle_to_index(handle, &index)) {
        return;
    }
    sk_colors[index] = normalize(r, g, b, a);
}

color_t sk_color_get(sk_handle_t handle)
{
    uint16_t index = 0;

    if (handle == 0) {
        return (color_t){1.0f, 1.0f, 1.0f, 1.0f}; /* white */
    }
    if (!resolve_color_handle_to_index(handle, &index)) {
        return (color_t){1.0f, 0.0f, 1.0f, 1.0f}; /* magenta = invalid */
    }
    return sk_colors[index];
}

void sk_color_init(void)
{
    memset(sk_colors, 0, sizeof(sk_colors));

    sk_handle_pool_init(&sk_color_pool,
                        SK_HANDLE_KIND_COLOR,
                        MAX_COLORS,
                        sk_color_free_indices,
                        MAX_COLORS,
                        sk_color_generations,
                        sk_color_occupied);

    for (uint16_t i = 1; i <= SK_COLOR_BUILTIN_COUNT; i++) {
        sk_color_generations[i] = 1;
        sk_color_occupied[i] = 1;
    }
    sk_color_pool.next_index = SK_COLOR_DYNAMIC_START_INDEX;

    sk_color_set(SK_COLOR_DEFAULT, 255, 0, 255, 255);
    sk_color_set(SK_COLOR_LIGHTGRAY, 200, 200, 200, 255);
    sk_color_set(SK_COLOR_GRAY, 130, 130, 130, 255);
    sk_color_set(SK_COLOR_DARKGRAY, 80, 80, 80, 255);
    sk_color_set(SK_COLOR_YELLOW, 255, 255, 0, 255);
    sk_color_set(SK_COLOR_GOLD, 255, 203, 0, 255);
    sk_color_set(SK_COLOR_ORANGE, 255, 161, 0, 255);
    sk_color_set(SK_COLOR_PINK, 255, 109, 194, 255);
    sk_color_set(SK_COLOR_RED, 230, 41, 55, 255);
    sk_color_set(SK_COLOR_MAROON, 190, 33, 45, 255);
    sk_color_set(SK_COLOR_GREEN, 0, 228, 48, 255);
    sk_color_set(SK_COLOR_LIME, 0, 158, 47, 255);
    sk_color_set(SK_COLOR_DARKGREEN, 0, 117, 44, 255);
    sk_color_set(SK_COLOR_SKYBLUE, 102, 191, 255, 255);
    sk_color_set(SK_COLOR_BLUE, 0, 121, 241, 255);
    sk_color_set(SK_COLOR_DARKBLUE, 0, 82, 172, 255);
    sk_color_set(SK_COLOR_PURPLE, 200, 122, 255, 255);
    sk_color_set(SK_COLOR_VIOLET, 135, 60, 190, 255);
    sk_color_set(SK_COLOR_DARKPURPLE, 112, 31, 126, 255);
    sk_color_set(SK_COLOR_BEIGE, 211, 176, 131, 255);
    sk_color_set(SK_COLOR_BROWN, 127, 106, 79, 255);
    sk_color_set(SK_COLOR_DARKBROWN, 76, 63, 47, 255);
    sk_color_set(SK_COLOR_WHITE, 255, 255, 255, 255);
    sk_color_set(SK_COLOR_BLACK, 0, 0, 0, 255);
    sk_color_set(SK_COLOR_BLANK, 0, 0, 0, 0);
    sk_color_set(SK_COLOR_MAGENTA, 255, 0, 255, 255);
    sk_color_set(SK_COLOR_RAYWHITE, 245, 245, 245, 255);
}

void sk_color_deinit(void)
{
    for (uint16_t i = 1; i < MAX_COLORS; i++) {
        if (sk_color_occupied[i]) {
            sk_colors[i] = (color_t){0};
        }
    }
    sk_handle_pool_reset(&sk_color_pool);
}
