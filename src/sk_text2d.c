#include "sk_text2d.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_font.h"
#include "internal/sk_handle_pool.h"
#include "sk_handle.h"
#include "sk_logger.h"
#include "sk_text.h" /* draw/measure delegate (handles the bitmap fallback) */

#include "fontstash.h" /* FONS_INVALID for the font-ready check */

/* Retained 2D text: holds the string + placement/color as instance state and
 * renders through the immediate sk_text path. Fonts aren't reference-counted
 * (sk_font owns the slot until destroyed), so the object just stores the handle;
 * an unset/not-ready font falls back to the built-in bitmap font. */

#define MAX_TEXT2D 256

typedef struct {
    sk_handle_t font; /* 0 or not-ready -> bitmap fallback */
    char *text;       /* owned copy, may be NULL */
    float x, y;
    float size;
    sk_handle_t color;
    bool visible;
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

static bool font_ready(sk_handle_t font)
{
    return sk_font_fons_id(font) != FONS_INVALID;
}

void sk_text2d_init(void)
{
    memset(sk_texts, 0, sizeof(sk_texts));
    sk_handle_pool_init(&sk_text2d_pool, SK_HANDLE_KIND_TEXT2D, MAX_TEXT2D,
                        sk_text2d_free_indices, MAX_TEXT2D,
                        sk_text2d_generations, sk_text2d_occupied);
}

void sk_text2d_deinit(void)
{
    for (int i = 0; i < MAX_TEXT2D; i++) {
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
    sk_texts[index] = (sk_text2d_t){
        .font = font,
        .text = NULL,
        .size = 16.0f,
        .color = 0,
        .visible = true,
    };
    return handle;
}

SK_KEEP
bool sk_text2d_set_font(sk_handle_t handle, sk_handle_t font)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->font = font; /* fonts aren't refcounted; the app owns the lifetime */
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
bool sk_text2d_set_color(sk_handle_t handle, sk_handle_t color)
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
float sk_text2d_measure_width(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || text_ptr->text == NULL) return 0.0f;
    if (font_ready(text_ptr->font)) {
        return sk_text_measure_ex(text_ptr->font, text_ptr->text, text_ptr->size).x;
    }
    return (float)sk_text_measure(text_ptr->text, (int)text_ptr->size);
}

SK_KEEP
float sk_text2d_measure_height(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || text_ptr->text == NULL) return 0.0f;
    if (font_ready(text_ptr->font)) {
        return sk_text_measure_ex(text_ptr->font, text_ptr->text, text_ptr->size).y;
    }
    return text_ptr->size; /* bitmap glyph height tracks the requested size */
}

SK_KEEP
void sk_text2d_draw(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL) return;
    /* delegates to TTF when the font resolves, else the built-in bitmap font */
    sk_text_draw_ex(text_ptr->font, text_ptr->text, text_ptr->x, text_ptr->y,
                    text_ptr->size, text_ptr->color);
}

SK_KEEP
void sk_text2d_destroy(sk_handle_t handle)
{
    sk_text2d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return;
    free(text_ptr->text);
    memset(text_ptr, 0, sizeof(*text_ptr));
    sk_handle_pool_free(&sk_text2d_pool, handle);
}
