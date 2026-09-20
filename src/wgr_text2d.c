#include "wgr_text2d.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "wgr_color.h"
#include "internal/wgr_font_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_module_internal.h"
#include "wgr_handle.h"
#include "wgr_logger.h"
#include "wgr_text.h" /* draw/measure delegate (resolves font 0 to the default font) */


/* Retained 2D text: holds the string + placement/color as instance state and
 * renders through the immediate wgr_text path. Fonts aren't reference-counted
 * (a font the text uses stays alive: the text holds a reference); a font that
 * isn't set (0) uses the default font. */

#define TEXT2D_INITIAL 64 /* slots to start with; the pool doubles as needed */

typedef struct {
    wgr_handle_t font; /* referenced; 0 = the default font */
    char *text;       /* owned copy, may be NULL */
    float x, y;
    float size;
    float max_width; /* 0 = no wrap */
    wgr_text_align_t align_x, align_y;
    wgr_color_t color;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
} wgr_text2d_t;

static wgr_text2d_t *wgr_texts; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_text2d_pool;

static wgr_text2d_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_text2d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid text2d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &wgr_texts[index];
}

/* The block of text on screen: its top-left corner and its size. The position is the
 * block's left/center/right and top/middle/bottom edge, per its alignment; a wrapped
 * block is as wide as its max width, an unwrapped one as wide as its widest line. */
static bool block_rect(const wgr_text2d_t *text_ptr, float *left, float *top, float *width, float *height)
{
    const vec2_t size = wgr_text_block_size(text_ptr->font, text_ptr->text, -1, text_ptr->size, text_ptr->max_width);
    if (size.y <= 0.0f) {
        return false;
    }
    *width = text_ptr->max_width > 0.0f && text_ptr->max_width > size.x ? text_ptr->max_width : size.x;
    *height = size.y;
    *left = text_ptr->x - (text_ptr->align_x == WGR_TEXT_ALIGN_CENTER   ? *width * 0.5f
                           : text_ptr->align_x == WGR_TEXT_ALIGN_RIGHT ? *width
                                                                      : 0.0f);
    *top = text_ptr->y - (text_ptr->align_y == WGR_TEXT_ALIGN_MIDDLE   ? *height * 0.5f
                          : text_ptr->align_y == WGR_TEXT_ALIGN_BOTTOM ? *height
                                                                      : 0.0f);
    return true;
}

/* Scene 2D pass: drawn over 3D; picked by its block's rectangle. */
static void draw_2d(wgr_handle_t handle)
{
    wgr_text2d_draw(handle);
}

static bool pick_2d(wgr_handle_t handle, float x, float y, wgr_pick_result_t *out)
{
    const wgr_text2d_t *text_ptr = resolve(handle);
    float left, top, width, height;
    if (text_ptr == NULL || !text_ptr->visible || !text_ptr->pickable || text_ptr->text == NULL ||
        !block_rect(text_ptr, &left, &top, &width, &height)) {
        return false;
    }
    if (x < left || y < top || x > left + width || y > top + height) {
        return false;
    }
    *out = (wgr_pick_result_t){
        .hit = true,
        .point_local = {x - left, y - top, 0},
        .point_world = {x, y, 0},
    };
    return true;
}

void wgr_text2d_init(void)
{
    if (!wgr_handle_pool_init(&wgr_text2d_pool, WGR_HANDLE_KIND_TEXT2D, "text2d", (void **)&wgr_texts,
                             sizeof(wgr_text2d_t), TEXT2D_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("text2d: out of memory");
    }
    wgr_scene_register_2d(WGR_HANDLE_KIND_TEXT2D, draw_2d, pick_2d);
    wgr_scene_register_enabled(WGR_HANDLE_KIND_TEXT2D, wgr_text2d_is_enabled);
}

void wgr_text2d_deinit(void)
{
    for (int i = 0; i < wgr_text2d_pool.capacity; i++) {
        if (wgr_text2d_pool.occupied[i]) wgr_font_release(wgr_texts[i].font);
        free(wgr_texts[i].text);
        wgr_texts[i].text = NULL;
    }
    wgr_handle_pool_destroy(&wgr_text2d_pool);
}

WGR_KEEP
wgr_handle_t wgr_text2d_create(wgr_handle_t font)
{
    wgr_handle_t handle = wgr_handle_pool_alloc(&wgr_text2d_pool);
    uint16_t index = 0;
    if (handle == 0) {
        log_error("text2d: pool full (%u)", (unsigned)wgr_text2d_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_text2d_pool, handle, &index);
    wgr_font_retain(font); /* no-op for 0 */
    wgr_texts[index] = (wgr_text2d_t){
        .font = font,
        .text = NULL,
        .size = 16.0f,
        .align_x = WGR_TEXT_ALIGN_LEFT,
        .align_y = WGR_TEXT_ALIGN_TOP,
        .color = WGR_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

WGR_KEEP
bool wgr_text2d_set_font(wgr_handle_t handle, wgr_handle_t font)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    wgr_font_retain(font); /* before releasing, in case they're the same font */
    wgr_font_release(text_ptr->font);
    text_ptr->font = font;
    return true;
}

WGR_KEEP
bool wgr_text2d_set_text(wgr_handle_t handle, const char *text)
{
    wgr_text2d_t *text_ptr = resolve(handle);
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

WGR_KEEP
bool wgr_text2d_set_position(wgr_handle_t handle, float x, float y)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->x = x;
    text_ptr->y = y;
    return true;
}

WGR_KEEP
bool wgr_text2d_set_size(wgr_handle_t handle, float size)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->size = size;
    return true;
}

WGR_KEEP
bool wgr_text2d_set_color(wgr_handle_t handle, wgr_color_t color)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->color = color;
    return true;
}

WGR_KEEP
bool wgr_text2d_set_visible(wgr_handle_t handle, bool visible)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->visible = visible;
    return true;
}

WGR_KEEP
bool wgr_text2d_is_visible(wgr_handle_t handle)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->visible;
}

WGR_KEEP
bool wgr_text2d_set_align(wgr_handle_t handle, wgr_text_align_t horizontal, wgr_text_align_t vertical)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    if (horizontal > WGR_TEXT_ALIGN_RIGHT || vertical < WGR_TEXT_ALIGN_TOP || vertical > WGR_TEXT_ALIGN_BOTTOM) {
        log_warn("wgr_text2d_set_align: horizontal is LEFT/CENTER/RIGHT, vertical TOP/MIDDLE/BOTTOM");
        return false;
    }
    text_ptr->align_x = horizontal;
    text_ptr->align_y = vertical;
    return true;
}

WGR_KEEP
bool wgr_text2d_set_max_width(wgr_handle_t handle, float width)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->max_width = width > 0.0f ? width : 0.0f;
    return true;
}

WGR_KEEP
float wgr_text2d_measure_width(wgr_handle_t handle)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || text_ptr->text == NULL) return 0.0f;
    return wgr_text_block_size(text_ptr->font, text_ptr->text, -1, text_ptr->size, text_ptr->max_width).x;
}

WGR_KEEP
float wgr_text2d_measure_height(wgr_handle_t handle)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || text_ptr->text == NULL) return 0.0f;
    return wgr_text_block_size(text_ptr->font, text_ptr->text, -1, text_ptr->size, text_ptr->max_width).y;
}

WGR_KEEP
bool wgr_text2d_set_pickable(wgr_handle_t handle, bool pickable)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->pickable = pickable;
    return true;
}

WGR_KEEP
bool wgr_text2d_is_pickable(wgr_handle_t handle)
{
    const wgr_text2d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->pickable;
}

WGR_KEEP
bool wgr_text2d_set_enabled(wgr_handle_t handle, bool enabled)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->enabled = enabled;
    return true;
}

WGR_KEEP
bool wgr_text2d_is_enabled(wgr_handle_t handle)
{
    const wgr_text2d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->enabled;
}

WGR_KEEP
void wgr_text2d_draw(wgr_handle_t handle)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    float left, top, width, height;
    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL ||
        !block_rect(text_ptr, &left, &top, &width, &height)) {
        return;
    }
    /* font 0 or a font that isn't loaded resolves to the default / built-in font */
    wgr_text_block_draw(text_ptr->font, text_ptr->text, -1, left, top, text_ptr->size, text_ptr->color,
                       text_ptr->max_width, width, text_ptr->align_x);
}

WGR_KEEP
void wgr_text2d_destroy(wgr_handle_t handle)
{
    wgr_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return;
    wgr_scene_forget(handle);
    free(text_ptr->text);
    wgr_font_release(text_ptr->font);
    memset(text_ptr, 0, sizeof(*text_ptr));
    wgr_handle_pool_free(&wgr_text2d_pool, handle);
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgr_module.h). */
static wgr_module_t wgr_text2d_module = {.name = "text2d", .order = 80, .init = wgr_text2d_init, .deinit = wgr_text2d_deinit};
WGR_MODULE(wgr_text2d_module)
