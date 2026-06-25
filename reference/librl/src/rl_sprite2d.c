#include "rl_sprite2d.h"

#include <raylib.h>
#include <stdbool.h>

#include "internal/exports.h"
#include "internal/rl_color.h"
#include "internal/rl_handle_pool.h"
#include "internal/rl_sprite2d.h"
#include "rl_texture.h"
#include "internal/rl_scene.h"
#include "internal/rl_texture.h"

#define MAX_SPRITE2D 1024

typedef struct
{
    bool in_use;
    rl_handle_t texture;
    float x;
    float y;
    float scale;
    float rotation;
    rl_handle_t tint_handle;
    bool visible;
    bool pickable;
} rl_sprite2d_instance_t;

static rl_sprite2d_instance_t rl_sprite2d[MAX_SPRITE2D];
static rl_handle_pool_t rl_sprite2d_pool;
static uint16_t rl_sprite2d_free_indices[MAX_SPRITE2D];
static uint16_t rl_sprite2d_generations[MAX_SPRITE2D];
static unsigned char rl_sprite2d_occupied[MAX_SPRITE2D];

static rl_sprite2d_instance_t *resolve_sprite2d_instance(rl_handle_t handle)
{
    uint16_t index = 0;
    if (!rl_handle_pool_resolve(&rl_sprite2d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid sprite2d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_sprite2d[index].in_use) {
        log_warn("Stale sprite2d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_sprite2d[index];
}

RL_KEEP
rl_handle_t rl_sprite2d_get_default_texture(void)
{
    return rl_texture_get_default();
}

RL_KEEP
rl_handle_t rl_sprite2d_create(rl_handle_t texture)
{
    rl_handle_t handle = 0;
    uint16_t index = 0;

    if (texture != 0 && !rl_texture_retain(texture)) {
        log_warn("Invalid texture handle (%u) for rl_sprite2d_create", texture);
        return 0;
    }

    handle = rl_handle_pool_alloc(&rl_sprite2d_pool);
    if (handle == 0) {
        log_error("MAX_SPRITE2D reached (%u)", MAX_SPRITE2D);
        if (texture != 0) rl_texture_release(texture);
        return 0;
    }
    rl_handle_pool_resolve(&rl_sprite2d_pool, handle, &index);

    rl_sprite2d[index].texture = texture;
    rl_sprite2d[index].x = 0.0f;
    rl_sprite2d[index].y = 0.0f;
    rl_sprite2d[index].scale = 1.0f;
    rl_sprite2d[index].rotation = 0.0f;
    rl_sprite2d[index].tint_handle = 0;
    rl_sprite2d[index].visible = true;
    rl_sprite2d[index].pickable = false;
    rl_sprite2d[index].in_use = true;

    return handle;
}

RL_KEEP
rl_handle_t rl_sprite2d_create_from_file(const char *filename)
{
    rl_handle_t texture = rl_texture_create(filename);
    rl_handle_t handle = 0;
    uint16_t index = 0;

    if (texture == 0) {
        return 0;
    }

    handle = rl_handle_pool_alloc(&rl_sprite2d_pool);
    if (handle == 0) {
        log_error("MAX_SPRITE2D reached (%u)", MAX_SPRITE2D);
        rl_texture_destroy(texture);
        return 0;
    }
    rl_handle_pool_resolve(&rl_sprite2d_pool, handle, &index);

    rl_sprite2d[index].texture = texture;
    rl_sprite2d[index].x = 0.0f;
    rl_sprite2d[index].y = 0.0f;
    rl_sprite2d[index].scale = 1.0f;
    rl_sprite2d[index].rotation = 0.0f;
    rl_sprite2d[index].tint_handle = 0;
    rl_sprite2d[index].visible = true;
    rl_sprite2d[index].pickable = false;
    rl_sprite2d[index].in_use = true;

    return handle;
}

RL_KEEP
bool rl_sprite2d_set_texture(rl_handle_t handle, rl_handle_t texture)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);
    if (sprite == NULL) return false;
    if (texture != 0 && !rl_texture_retain(texture)) {
        log_warn("Invalid texture handle (%u) for rl_sprite2d_set_texture", texture);
        return false;
    }
    if (sprite->texture != 0) rl_texture_release(sprite->texture);
    sprite->texture = texture;
    return true;
}

RL_KEEP
bool rl_sprite2d_set_transform(rl_handle_t handle,
                               float x, float y,
                               float scale, float rotation)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);
    if (sprite == NULL) {
        return false;
    }
    sprite->x = x;
    sprite->y = y;
    sprite->scale = scale;
    sprite->rotation = rotation;
    return true;
}

RL_KEEP
bool rl_sprite2d_set_visible(rl_handle_t handle, bool visible)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    sprite->visible = visible;
    return true;
}

RL_KEEP
bool rl_sprite2d_set_pickable(rl_handle_t handle, bool pickable)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    sprite->pickable = pickable;
    return true;
}

RL_KEEP
bool rl_sprite2d_is_visible(rl_handle_t handle)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    return sprite->visible;
}

RL_KEEP
bool rl_sprite2d_is_pickable(rl_handle_t handle)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    return sprite->pickable;
}

RL_KEEP
void rl_sprite2d_draw(rl_handle_t handle)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);
    Texture2D *texture = NULL;
    if (sprite == NULL) {
        if (handle != 0) {
            DrawTextureEx(*rl_texture_get_placeholder(),
                          (Vector2){0.0f, 0.0f}, 0.0f, 1.0f,
                          (Color){255, 0, 255, 255});
        }
        return;
    }
    if (!sprite->visible) {
        return;
    }
    if (sprite->texture == 0) {
        return;
    }
    texture = rl_texture_get_ptr(sprite->texture);
    if (texture == NULL) {
        log_error("Missing texture for sprite2d handle (%u)", handle);
        return;
    }
    DrawTextureEx(*texture,
                  (Vector2){sprite->x, sprite->y},
                  sprite->rotation,
                  sprite->scale,
                  rl_color_get(sprite->tint_handle));
}

RL_KEEP
bool rl_sprite2d_set_tint(rl_handle_t handle, rl_handle_t color_handle)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);
    if (sprite == NULL) return false;
    sprite->tint_handle = color_handle;
    return true;
}

RL_KEEP
void rl_sprite2d_destroy(rl_handle_t handle)
{
    rl_sprite2d_instance_t *sprite = resolve_sprite2d_instance(handle);
    if (sprite == NULL) {
        return;
    }
    if (sprite->texture != 0) rl_texture_release(sprite->texture);
    sprite->texture = 0;
    sprite->in_use = false;
    rl_scene_on_drawable_destroy(handle);
    rl_handle_pool_free(&rl_sprite2d_pool, handle);
}

void rl_sprite2d_init(void)
{
    rl_handle_pool_init(&rl_sprite2d_pool,
                        RL_HANDLE_KIND_SPRITE2D,
                        MAX_SPRITE2D,
                        rl_sprite2d_free_indices,
                        MAX_SPRITE2D,
                        rl_sprite2d_generations,
                        rl_sprite2d_occupied);
    for (int i = 0; i < MAX_SPRITE2D; i++) {
        rl_sprite2d[i].in_use = false;
        rl_sprite2d[i].texture = 0;
        rl_sprite2d[i].x = 0.0f;
        rl_sprite2d[i].y = 0.0f;
        rl_sprite2d[i].scale = 1.0f;
        rl_sprite2d[i].rotation = 0.0f;
        rl_sprite2d[i].tint_handle = 0;
        rl_sprite2d[i].visible = true;
        rl_sprite2d[i].pickable = false;
    }
}

void rl_sprite2d_deinit(void)
{
    int unloaded = 0;
    for (uint16_t i = 1; i < MAX_SPRITE2D; i++) {
        if (!rl_sprite2d[i].in_use) {
            continue;
        }
        {
            rl_handle_t handle = rl_handle_pool_handle_from_index(&rl_sprite2d_pool, i);
            if (handle == 0) {
                continue;
            }
            rl_sprite2d_destroy(handle);
            unloaded++;
        }
    }
    rl_handle_pool_reset(&rl_sprite2d_pool);
    log_info("rl_sprite2d_deinit: Freed %d sprite2d instances", unloaded);
}
