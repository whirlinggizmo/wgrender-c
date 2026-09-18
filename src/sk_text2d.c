#include "sk_text2d.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "sk_color.h"
#include "internal/sk_font.h"
#include "internal/sk_internal.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_scene.h"
#include "sk_handle.h"
#include "sk_logger.h"
#include "sk_text.h" /* draw/measure delegate (resolves font 0 to the default font) */


/* Retained 2D text: holds the string + placement/color as instance state and
 * renders through the immediate sk_text path. Fonts aren't reference-counted
 * (a font the text uses stays alive: the text holds a reference); a font that
 * isn't set (0) uses the default font. */

#define MAX_TEXT2D 256

typedef struct {
    sk_handle_t font; /* referenced; 0 = the default font */
    char *text;       /* owned copy, may be NULL */
    float x, y;
    float size;
    float max_width; /* 0 = no wrap */
    sk_text_align_t align_x, align_y;
    sk_color_t color;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
} sk_text2d_t;

static sk_text2d_t sk_texts[MAX_TEXT2D];
static sk_handle_pool_t sk_text2d_pool;
static uint16_t sk_text2d_free_indices[MAX_TEXT2D];
static uint16_t sk_text2d_generations[MAX_TEXT2D];
static unsigned char sk_text2d_occupied[MAX_TEXT2D];

static sk_text2d_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_text2d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid text2d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_texts[index];
}

/* The block of text on screen: its top-left corner and its size. The position is the
 * block's left/center/right and top/middle/bottom edge, per its alignment; a wrapped
 * block is as wide as its max width, an unwrapped one as wide as its widest line. */
static bool block_rect(const sk_text2d_t *text_ptr, float *left, float *top, float *width, float *height)
{
    const vec2_t size = sk_text_block_size(text_ptr->font, text_ptr->text, -1, text_ptr->size, text_ptr->max_width);
    if (size.y <= 0.0f) {
        return false;
    }
    *width = text_ptr->max_width > 0.0f && text_ptr->max_width > size.x ? text_ptr->max_width : size.x;
    *height = size.y;
    *left = text_ptr->x - (text_ptr->align_x == SK_TEXT_ALIGN_CENTER   ? *width * 0.5f
                           : text_ptr->align_x == SK_TEXT_ALIGN_RIGHT ? *width
                                                                      : 0.0f);
    *top = text_ptr->y - (text_ptr->align_y == SK_TEXT_ALIGN_MIDDLE   ? *height * 0.5f
                          : text_ptr->align_y == SK_TEXT_ALIGN_BOTTOM ? *height
                                                                      : 0.0f);
    return true;
}

/* Scene 2D pass: drawn over 3D; picked by its block's rectangle. */
static void draw_2d(sk_handle_t handle)
{
    sk_text2d_draw(handle);
}

static bool pick_2d(sk_handle_t handle, float x, float y, sk_pick_result_t *out)
{
    const sk_text2d_t *text_ptr = resolve(handle);
    float left, top, width, height;
    if (text_ptr == NULL || !text_ptr->visible || !text_ptr->pickable || text_ptr->text == NULL ||
        !block_rect(text_ptr, &left, &top, &width, &height)) {
        return false;
    }
    if (x < left || y < top || x > left + width || y > top + height) {
        return false;
    }
    *out = (sk_pick_result_t){
        .hit = true,
        .point_local = {x - left, y - top, 0},
        .point_world = {x, y, 0},
    };
    return true;
}

void sk_text2d_init(void)
{
    memset(sk_texts, 0, sizeof(sk_texts));
    sk_handle_pool_init(&sk_text2d_pool, SK_HANDLE_KIND_TEXT2D, MAX_TEXT2D,
                        sk_text2d_free_indices, MAX_TEXT2D,
                        sk_text2d_generations, sk_text2d_occupied);
    sk_scene_register_2d(SK_HANDLE_KIND_TEXT2D, draw_2d, pick_2d);
    sk_scene_register_enabled(SK_HANDLE_KIND_TEXT2D, sk_text2d_is_enabled);
}

void sk_text2d_deinit(void)
{
    for (int i = 0; i < MAX_TEXT2D; i++) {
        if (sk_text2d_occupied[i]) sk_font_release(sk_texts[i].font);
        free(sk_texts[i].text);
        sk_texts[i].text = NULL;
    }
    sk_handle_pool_reset(&sk_text2d_pool);
}

SK_KEEP
sk_handle_t sk_text2d_create(sk_handle_t font)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_text2d_pool);
    uint16_t index = 0;
    if (handle == 0) {
        log_error("MAX_TEXT2D reached (%d)", MAX_TEXT2D);
        return 0;
    }
    sk_handle_pool_resolve(&sk_text2d_pool, handle, &index);
    sk_font_retain(font); /* no-op for 0 */
    sk_texts[index] = (sk_text2d_t){
        .font = font,
        .text = NULL,
        .size = 16.0f,
        .align_x = SK_TEXT_ALIGN_LEFT,
        .align_y = SK_TEXT_ALIGN_TOP,
        .color = SK_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

SK_KEEP
bool sk_text2d_set_font(sk_handle_t handle, sk_handle_t font)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    sk_font_retain(font); /* before releasing, in case they're the same font */
    sk_font_release(text_ptr->font);
    text_ptr->font = font;
    return true;
}

SK_KEEP
bool sk_text2d_set_text(sk_handle_t handle, const char *text)
{
    sk_text2d_t *text_ptr = resolve(handle);
    char *copy = NULL;
    if (text_ptr == NULL) return false;
    if (text != NULL) {
        size_t n = strlen(text);
        copy = (char *)malloc(n + 1);
        if (copy == NULL) return false;
        memcpy(copy, text, n + 1);
    }
    free(text_ptr->text);
    text_ptr->text = copy;
    return true;
}

SK_KEEP
bool sk_text2d_set_position(sk_handle_t handle, float x, float y)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->x = x;
    text_ptr->y = y;
    return true;
}

SK_KEEP
bool sk_text2d_set_size(sk_handle_t handle, float size)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->size = size;
    return true;
}

SK_KEEP
bool sk_text2d_set_color(sk_handle_t handle, sk_color_t color)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->color = color;
    return true;
}

SK_KEEP
bool sk_text2d_set_visible(sk_handle_t handle, bool visible)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->visible = visible;
    return true;
}

SK_KEEP
bool sk_text2d_is_visible(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->visible;
}

SK_KEEP
bool sk_text2d_set_align(sk_handle_t handle, sk_text_align_t horizontal, sk_text_align_t vertical)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    if (horizontal > SK_TEXT_ALIGN_RIGHT || vertical < SK_TEXT_ALIGN_TOP || vertical > SK_TEXT_ALIGN_BOTTOM) {
        log_warn("sk_text2d_set_align: horizontal is LEFT/CENTER/RIGHT, vertical TOP/MIDDLE/BOTTOM");
        return false;
    }
    text_ptr->align_x = horizontal;
    text_ptr->align_y = vertical;
    return true;
}

SK_KEEP
bool sk_text2d_set_max_width(sk_handle_t handle, float width)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->max_width = width > 0.0f ? width : 0.0f;
    return true;
}

SK_KEEP
float sk_text2d_measure_width(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || text_ptr->text == NULL) return 0.0f;
    return sk_text_block_size(text_ptr->font, text_ptr->text, -1, text_ptr->size, text_ptr->max_width).x;
}

SK_KEEP
float sk_text2d_measure_height(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || text_ptr->text == NULL) return 0.0f;
    return sk_text_block_size(text_ptr->font, text_ptr->text, -1, text_ptr->size, text_ptr->max_width).y;
}

SK_KEEP
bool sk_text2d_set_pickable(sk_handle_t handle, bool pickable)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->pickable = pickable;
    return true;
}

SK_KEEP
bool sk_text2d_is_pickable(sk_handle_t handle)
{
    const sk_text2d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->pickable;
}

SK_KEEP
bool sk_text2d_set_enabled(sk_handle_t handle, bool enabled)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->enabled = enabled;
    return true;
}

SK_KEEP
bool sk_text2d_is_enabled(sk_handle_t handle)
{
    const sk_text2d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->enabled;
}

SK_KEEP
void sk_text2d_draw(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    float left, top, width, height;
    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL ||
        !block_rect(text_ptr, &left, &top, &width, &height)) {
        return;
    }
    /* font 0 or a font that isn't loaded resolves to the default / built-in font */
    sk_text_block_draw(text_ptr->font, text_ptr->text, -1, left, top, text_ptr->size, text_ptr->color,
                       text_ptr->max_width, width, text_ptr->align_x);
}

SK_KEEP
void sk_text2d_destroy(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return;
    free(text_ptr->text);
    sk_font_release(text_ptr->font);
    memset(text_ptr, 0, sizeof(*text_ptr));
    sk_handle_pool_free(&sk_text2d_pool, handle);
}
