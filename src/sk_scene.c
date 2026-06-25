#include "sk_scene.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_math.h"
#include "internal/sk_scene.h"
#include "internal/sk_pick.h"
#include "sk_camera3d.h"
#include "sk_logger.h"
#include "sk_render.h"
#include "sk_window.h"

#define MAX_SCENES 64
#define SK_DRAWABLE_KIND_COUNT 64 /* handle kind is 6 bits */

typedef struct {
    sk_handle_t drawable;
    int layer;
} sk_scene_entry_t;

typedef struct {
    sk_scene_entry_t *items;
    int count;
    int capacity;
    sk_handle_t camera;
} sk_scene_data_t;

static sk_scene_data_t sk_scenes[MAX_SCENES];
static sk_handle_pool_t sk_scene_pool;
static uint16_t sk_scene_free_indices[MAX_SCENES];
static uint16_t sk_scene_generations[MAX_SCENES];
static unsigned char sk_scene_occupied[MAX_SCENES];

static sk_drawable_draw_fn sk_drawable_registry[SK_DRAWABLE_KIND_COUNT];
static sk_drawable_bounds_fn sk_bounds_registry[SK_DRAWABLE_KIND_COUNT];
static sk_drawable_pick_fn sk_pick_registry[SK_DRAWABLE_KIND_COUNT];

/* ---- drawable dispatch registry --------------------------------------- */

void sk_scene_register_drawable(sk_handle_kind_t kind, sk_drawable_draw_fn draw)
{
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT) {
        return;
    }
    sk_drawable_registry[kind] = draw;
}

void sk_drawable_draw(sk_handle_t handle)
{
    sk_handle_kind_t kind = sk_handle_get_kind(handle);
    if ((int)kind >= 0 && (int)kind < SK_DRAWABLE_KIND_COUNT &&
        sk_drawable_registry[kind] != NULL) {
        sk_drawable_registry[kind](handle);
    }
}

void sk_scene_register_bounds(sk_handle_kind_t kind, sk_drawable_bounds_fn bounds)
{
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT) {
        return;
    }
    sk_bounds_registry[kind] = bounds;
}

bool sk_drawable_bounds(sk_handle_t handle, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model)
{
    sk_handle_kind_t kind = sk_handle_get_kind(handle);
    if ((int)kind >= 0 && (int)kind < SK_DRAWABLE_KIND_COUNT &&
        sk_bounds_registry[kind] != NULL) {
        return sk_bounds_registry[kind](handle, lmin, lmax, model);
    }
    return false;
}

void sk_scene_register_pick(sk_handle_kind_t kind, sk_drawable_pick_fn pick)
{
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT) {
        return;
    }
    sk_pick_registry[kind] = pick;
}

bool sk_drawable_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out)
{
    sk_handle_kind_t kind = sk_handle_get_kind(handle);
    if ((int)kind >= 0 && (int)kind < SK_DRAWABLE_KIND_COUNT &&
        sk_pick_registry[kind] != NULL) {
        return sk_pick_registry[kind](handle, origin, dir, out);
    }
    return false;
}

/* ---- scene store ------------------------------------------------------- */

static sk_scene_data_t *resolve(sk_handle_t scene)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_scene_pool, scene, &index)) {
        if (scene != 0) {
            log_warn("Invalid scene handle (%u)", (unsigned int)scene);
        }
        return NULL;
    }
    return &sk_scenes[index];
}

static int find_entry(sk_scene_data_t *s, sk_handle_t drawable)
{
    for (int i = 0; i < s->count; i++) {
        if (s->items[i].drawable == drawable) {
            return i;
        }
    }
    return -1;
}

SK_KEEP
sk_handle_t sk_scene_create(void)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_scene_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_SCENES reached (%d)", MAX_SCENES);
        return 0;
    }
    sk_handle_pool_resolve(&sk_scene_pool, handle, &index);
    sk_scenes[index] = (sk_scene_data_t){0};
    return handle;
}

SK_KEEP
void sk_scene_destroy(sk_handle_t scene)
{
    sk_scene_data_t *s = resolve(scene);
    if (s == NULL) {
        return;
    }
    free(s->items);
    *s = (sk_scene_data_t){0};
    sk_handle_pool_free(&sk_scene_pool, scene);
}

SK_KEEP
bool sk_scene_add(sk_handle_t scene, sk_handle_t drawable, int layer)
{
    sk_scene_data_t *s = resolve(scene);
    int existing;

    if (s == NULL || drawable == 0) {
        return false;
    }

    existing = find_entry(s, drawable);
    if (existing >= 0) {
        s->items[existing].layer = layer;
        return true;
    }

    if (s->count >= s->capacity) {
        int cap = s->capacity > 0 ? s->capacity * 2 : 8;
        sk_scene_entry_t *grown = realloc(s->items, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        s->items = grown;
        s->capacity = cap;
    }

    s->items[s->count].drawable = drawable;
    s->items[s->count].layer = layer;
    s->count++;
    return true;
}

SK_KEEP
bool sk_scene_set_layer(sk_handle_t scene, sk_handle_t drawable, int layer)
{
    sk_scene_data_t *s = resolve(scene);
    int idx;
    if (s == NULL) {
        return false;
    }
    idx = find_entry(s, drawable);
    if (idx < 0) {
        return false;
    }
    s->items[idx].layer = layer;
    return true;
}

SK_KEEP
bool sk_scene_remove(sk_handle_t scene, sk_handle_t drawable)
{
    sk_scene_data_t *s = resolve(scene);
    int idx;
    if (s == NULL) {
        return false;
    }
    idx = find_entry(s, drawable);
    if (idx < 0) {
        return false;
    }
    /* preserve order (stable for equal layers) */
    memmove(&s->items[idx], &s->items[idx + 1],
            (size_t)(s->count - idx - 1) * sizeof(sk_scene_entry_t));
    s->count--;
    return true;
}

SK_KEEP
void sk_scene_clear(sk_handle_t scene)
{
    sk_scene_data_t *s = resolve(scene);
    if (s == NULL) {
        return;
    }
    s->count = 0;
}

SK_KEEP
void sk_scene_set_active_camera(sk_handle_t scene, sk_handle_t camera)
{
    sk_scene_data_t *s = resolve(scene);
    if (s == NULL) {
        return;
    }
    s->camera = camera;
}

SK_KEEP
void sk_scene_draw(sk_handle_t scene)
{
    sk_scene_data_t *s = resolve(scene);
    if (s == NULL) {
        return;
    }

    if (s->camera != 0) {
        sk_camera3d_set_active(s->camera);
    }

    /* insertion sort by layer ascending (stable, small lists) */
    for (int i = 1; i < s->count; i++) {
        sk_scene_entry_t key = s->items[i];
        int j = i - 1;
        while (j >= 0 && s->items[j].layer > key.layer) {
            s->items[j + 1] = s->items[j];
            j--;
        }
        s->items[j + 1] = key;
    }

    sk_render_begin_mode_3d();
    for (int i = 0; i < s->count; i++) {
        sk_drawable_draw(s->items[i].drawable);
    }
    sk_render_end_mode_3d();
}

/* ---- picking ----------------------------------------------------------- */

SK_KEEP
sk_pick_result_t sk_scene_pick(sk_handle_t scene, sk_handle_t camera,
                               float mouse_x, float mouse_y)
{
    sk_pick_result_t result = {0};
    sk_scene_data_t *s = resolve(scene);
    sk_camera3d_data_t cam;
    sk_ray_t ray;
    vec2_t screen;
    float best_t = 1e30f;

    if (s == NULL) {
        return result;
    }

    if (camera == 0) {
        camera = s->camera;
    }
    if (camera != 0) {
        sk_camera3d_set_active(camera);
    }
    if (!sk_camera3d_get_active_data(&cam)) {
        return result;
    }

    screen = sk_window_get_screen_size();
    ray = sk_pick_ray_from_screen(&cam, mouse_x, mouse_y, screen.x, screen.y);

    for (int i = 0; i < s->count; i++) {
        sk_handle_t drawable = s->items[i].drawable;
        vec3_t lmin, lmax;
        sk_mat4_t model;
        sk_pick_result_t hit = {0};
        float broad_t;

        if (!sk_drawable_bounds(drawable, &lmin, &lmax, &model)) {
            continue;
        }
        if (!sk_pick_ray_world_aabb(ray, lmin, lmax, model, &broad_t)) {
            continue;
        }

        if (sk_drawable_pick(drawable, ray.origin, ray.dir, &hit)) {
            if (!hit.hit || hit.distance >= best_t) {
                continue;
            }
        } else {
            /* No narrow-phase handler: keep the broadphase (world AABB) hit. */
            vec3_t wp = {ray.origin.x + ray.dir.x * broad_t,
                         ray.origin.y + ray.dir.y * broad_t,
                         ray.origin.z + ray.dir.z * broad_t};
            hit.hit = true;
            hit.distance = broad_t;
            hit.point_world = wp;
            hit.point_local = sk_mat4_mul_point(sk_mat4_inverse(model), wp);
            if (hit.distance >= best_t) {
                continue;
            }
        }

        best_t = hit.distance;
        result = hit;
        result.handle = drawable;
    }
    return result;
}

void sk_scene_init(void)
{
    memset(sk_scenes, 0, sizeof(sk_scenes));
    memset(sk_drawable_registry, 0, sizeof(sk_drawable_registry));
    memset(sk_bounds_registry, 0, sizeof(sk_bounds_registry));
    memset(sk_pick_registry, 0, sizeof(sk_pick_registry));
    sk_handle_pool_init(&sk_scene_pool,
                        SK_HANDLE_KIND_SCENE,
                        MAX_SCENES,
                        sk_scene_free_indices,
                        MAX_SCENES,
                        sk_scene_generations,
                        sk_scene_occupied);
}

void sk_scene_deinit(void)
{
    for (int i = 0; i < MAX_SCENES; i++) {
        free(sk_scenes[i].items);
        sk_scenes[i] = (sk_scene_data_t){0};
    }
    sk_handle_pool_reset(&sk_scene_pool);
}
