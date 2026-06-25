#include "rl_text2d.h"

#include <raylib.h>
#include <stdbool.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/rl_color.h"
#include "internal/rl_font.h"
#include "internal/rl_handle_pool.h"
#include "internal/rl_scene.h"
#include "internal/rl_text2d.h"

#define MAX_TEXT2D 256
#define MAX_TEXT2D_CONTENT 512

typedef struct
{
    bool in_use;
    rl_handle_t font;
    rl_handle_t color;
    float size;
    float x;
    float y;
    char content[MAX_TEXT2D_CONTENT];
    bool visible;
    bool pickable;
} rl_text2d_instance_t;

static rl_text2d_instance_t rl_text2d[MAX_TEXT2D];
static rl_handle_pool_t rl_text2d_pool;
static uint16_t rl_text2d_free_indices[MAX_TEXT2D];
static uint16_t rl_text2d_generations[MAX_TEXT2D];
static unsigned char rl_text2d_occupied[MAX_TEXT2D];

static rl_text2d_instance_t *resolve_text2d_instance(rl_handle_t handle)
{
    uint16_t index = 0;
    if (!rl_handle_pool_resolve(&rl_text2d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid text2d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_text2d[index].in_use) {
        log_warn("Stale text2d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_text2d[index];
}

RL_KEEP
rl_handle_t rl_text2d_create(rl_handle_t font, float size)
{
    rl_handle_t handle = 0;
    uint16_t index = 0;

    handle = rl_handle_pool_alloc(&rl_text2d_pool);
    if (handle == 0) {
        log_error("MAX_TEXT2D reached (%u)", MAX_TEXT2D);
        return 0;
    }
    rl_handle_pool_resolve(&rl_text2d_pool, handle, &index);

    rl_text2d[index].font = font;
    rl_text2d[index].color = 0;
    rl_text2d[index].size = size;
    rl_text2d[index].x = 0.0f;
    rl_text2d[index].y = 0.0f;
    rl_text2d[index].content[0] = '\0';
    rl_text2d[index].visible = true;
    rl_text2d[index].pickable = false;
    rl_text2d[index].in_use = true;

    return handle;
}

RL_KEEP
void rl_text2d_set_font(rl_handle_t handle, rl_handle_t font)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);
    if (text == NULL) return;
    text->font = font;
}

RL_KEEP
void rl_text2d_set_size(rl_handle_t handle, float size)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);
    if (text == NULL) return;
    text->size = size;
}

RL_KEEP
void rl_text2d_set_content(rl_handle_t handle, const char *content)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);
    if (text == NULL) return;
    strncpy(text->content, content, MAX_TEXT2D_CONTENT - 1);
    text->content[MAX_TEXT2D_CONTENT - 1] = '\0';
}

RL_KEEP
void rl_text2d_set_position(rl_handle_t handle, float x, float y)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);
    if (text == NULL) return;
    text->x = x;
    text->y = y;
}

RL_KEEP
void rl_text2d_set_color(rl_handle_t handle, rl_handle_t color)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);
    if (text == NULL) return;
    text->color = color;
}

RL_KEEP
bool rl_text2d_set_visible(rl_handle_t handle, bool visible)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);

    if (text == NULL) {
        return false;
    }
    text->visible = visible;
    return true;
}

RL_KEEP
bool rl_text2d_set_pickable(rl_handle_t handle, bool pickable)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);

    if (text == NULL) {
        return false;
    }
    text->pickable = pickable;
    return true;
}

RL_KEEP
bool rl_text2d_is_visible(rl_handle_t handle)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);

    if (text == NULL) {
        return false;
    }
    return text->visible;
}

RL_KEEP
bool rl_text2d_is_pickable(rl_handle_t handle)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);

    if (text == NULL) {
        return false;
    }
    return text->pickable;
}

RL_KEEP
void rl_text2d_draw(rl_handle_t handle)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);
    if (text == NULL) return;
    if (!text->visible) return;
    if (text->content[0] == '\0') return;
    if (text->font == 0) return;

    Font font = rl_font_get(text->font);
    Color color = rl_color_get(text->color);
    DrawTextEx(font,
               text->content,
               (Vector2){text->x, text->y},
               text->size,
               1.0f,
               color);
}

RL_KEEP
void rl_text2d_destroy(rl_handle_t handle)
{
    rl_text2d_instance_t *text = resolve_text2d_instance(handle);
    if (text == NULL) return;
    text->font = 0;
    text->color = 0;
    text->content[0] = '\0';
    text->in_use = false;
    rl_scene_on_drawable_destroy(handle);
    rl_handle_pool_free(&rl_text2d_pool, handle);
}

void rl_text2d_init(void)
{
    rl_handle_pool_init(&rl_text2d_pool,
                        RL_HANDLE_KIND_TEXT2D,
                        MAX_TEXT2D,
                        rl_text2d_free_indices,
                        MAX_TEXT2D,
                        rl_text2d_generations,
                        rl_text2d_occupied);
    for (int i = 0; i < MAX_TEXT2D; i++) {
        rl_text2d[i].in_use = false;
        rl_text2d[i].font = 0;
        rl_text2d[i].color = 0;
        rl_text2d[i].size = 0.0f;
        rl_text2d[i].x = 0.0f;
        rl_text2d[i].y = 0.0f;
        rl_text2d[i].content[0] = '\0';
        rl_text2d[i].visible = true;
        rl_text2d[i].pickable = false;
    }
}

void rl_text2d_deinit(void)
{
    int unloaded = 0;
    for (uint16_t i = 1; i < MAX_TEXT2D; i++) {
        if (!rl_text2d[i].in_use) continue;
        rl_handle_t handle = rl_handle_pool_handle_from_index(&rl_text2d_pool, i);
        if (handle == 0) continue;
        rl_text2d_destroy(handle);
        unloaded++;
    }
    rl_handle_pool_reset(&rl_text2d_pool);
    log_info("rl_text2d_deinit: Freed %d text2d instances", unloaded);
}
