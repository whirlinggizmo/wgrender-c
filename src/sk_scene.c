#include "sk_scene.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_environment.h"
#include "internal/sk_light.h"
#include "internal/sk_math.h"
#include "internal/sk_scene.h"
#include "internal/sk_pick.h"
#include "internal/sk_render.h"
#include "sk_camera3d.h"
#include "sk_pick.h"
#include "sk_logger.h"
#include "sk_render.h"
#include "sk_window.h"

#define MAX_SCENES 64
#define SK_DRAWABLE_KIND_COUNT 64 /* handle kind is 6 bits */
#define MAX_TRANSPARENT_ITEMS 4096 /* per scene layer */

typedef struct {
    sk_handle_t drawable;
    int layer;
} sk_scene_entry_t;

typedef struct {
    sk_scene_entry_t *items;
    int count;
    int capacity;
    sk_handle_t camera;
    sk_handle_t ambient_color;  /* 0 = white */
    float ambient_intensity;    /* 0 = no ambient (default) */
    sk_handle_t environment;    /* referenced; 0 = none */
    float environment_intensity;
    float environment_rotation;
    sk_handle_t background;     /* referenced; 0 = none */
    float background_blur;
    sk_tonemap_t tonemap;
    float exposure;
} sk_scene_t;

static sk_scene_t sk_scenes[MAX_SCENES];
static sk_handle_pool_t sk_scene_pool;
static uint16_t sk_scene_free_indices[MAX_SCENES];
static uint16_t sk_scene_generations[MAX_SCENES];
static unsigned char sk_scene_occupied[MAX_SCENES];

typedef struct {
    sk_drawable_draw_opaque_fn draw_opaque;
    sk_drawable_collect_transparent_fn collect_transparent;
    sk_drawable_draw_transparent_fn draw_transparent;
    sk_drawable_draw_2d_fn draw_2d;
    sk_drawable_pick_2d_fn pick_2d;
} sk_drawable_passes_t;

static sk_drawable_passes_t sk_passes_registry[SK_DRAWABLE_KIND_COUNT];
static sk_transparent_item_t sk_transparent_items[MAX_TRANSPARENT_ITEMS];
static bool sk_transparent_overflow_logged;
static sk_drawable_bounds_fn sk_bounds_registry[SK_DRAWABLE_KIND_COUNT];
static sk_drawable_pick_fn sk_pick_registry[SK_DRAWABLE_KIND_COUNT];

/* ---- drawable dispatch registry --------------------------------------- */

void sk_scene_register_passes(sk_handle_kind_t kind,
                              sk_drawable_draw_opaque_fn draw_opaque,
                              sk_drawable_collect_transparent_fn collect_transparent,
                              sk_drawable_draw_transparent_fn draw_transparent)
{
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT) {
        return;
    }
    sk_passes_registry[kind].draw_opaque = draw_opaque;
    sk_passes_registry[kind].collect_transparent = collect_transparent;
    sk_passes_registry[kind].draw_transparent = draw_transparent;
}

void sk_scene_register_2d(sk_handle_kind_t kind, sk_drawable_draw_2d_fn draw, sk_drawable_pick_2d_fn pick)
{
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT) {
        return;
    }
    sk_passes_registry[kind].draw_2d = draw;
    sk_passes_registry[kind].pick_2d = pick;
}

static const sk_drawable_passes_t *lookup_passes(sk_handle_t handle)
{
    sk_handle_kind_t kind = sk_handle_get_kind(handle);
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT) {
        return NULL;
    }
    return &sk_passes_registry[kind];
}

float sk_scene_view_depth(const sk_camera3d_t *cam, vec3_t world_point)
{
    vec3_t forward = sk_v3_norm(sk_v3_sub(cam->target, cam->position));
    vec3_t offset = sk_v3_sub(world_point, cam->position);
    return offset.x * forward.x + offset.y * forward.y + offset.z * forward.z;
}

static int compare_transparent(const void *lhs, const void *rhs)
{
    const sk_transparent_item_t *a = (const sk_transparent_item_t *)lhs;
    const sk_transparent_item_t *b = (const sk_transparent_item_t *)rhs;
    if (a->depth != b->depth) {
        return a->depth > b->depth ? -1 : 1; /* farther first */
    }
    return (a->order > b->order) - (a->order < b->order);
}

void sk_scene_sort_transparent(sk_transparent_item_t *items, int count)
{
    if (count > 1) {
        qsort(items, (size_t)count, sizeof(items[0]), compare_transparent);
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

/* ---- picking one drawable ---------------------------------------------- */

static sk_pick_stats_t sk_pick_stats;

/* A 2D drawable at a screen point (logical pixels). */
static bool pick_2d(sk_handle_t drawable, const sk_drawable_passes_t *passes, float x, float y,
                    sk_pick_result_t *out)
{
    sk_pick_stats.narrowphase_tests++;
    if (!passes->pick_2d(drawable, x, y, out)) {
        *out = (sk_pick_result_t){0};
        return false;
    }
    sk_pick_stats.narrowphase_hits++;
    out->hit = true;
    out->handle = drawable;
    return true;
}

/* A 3D drawable along a world ray: bounding box first, then the kind's exact test.
 * Kinds without an exact test count a bounding box hit. */
static bool pick_3d(sk_handle_t drawable, sk_ray_t ray, sk_pick_result_t *out)
{
    const sk_handle_kind_t kind = sk_handle_get_kind(drawable);
    const bool has_exact = (int)kind >= 0 && (int)kind < SK_DRAWABLE_KIND_COUNT && sk_pick_registry[kind] != NULL;
    vec3_t lmin, lmax;
    sk_mat4_t model;
    float broad_t;

    *out = (sk_pick_result_t){0};
    if (!sk_drawable_bounds(drawable, &lmin, &lmax, &model)) {
        return false; /* not a 3D drawable, or nothing loaded yet */
    }
    sk_pick_stats.broadphase_tests++;
    if (!sk_pick_ray_world_aabb(ray, lmin, lmax, model, &broad_t)) {
        sk_pick_stats.broadphase_rejects++;
        return false;
    }
    if (has_exact) {
        sk_pick_stats.narrowphase_tests++;
        /* false: hidden or not pickable; no hit: missed */
        if (!sk_pick_registry[kind](drawable, ray.origin, ray.dir, out) || !out->hit) {
            *out = (sk_pick_result_t){0};
            return false;
        }
        sk_pick_stats.narrowphase_hits++;
    } else {
        const vec3_t wp = {ray.origin.x + ray.dir.x * broad_t, ray.origin.y + ray.dir.y * broad_t,
                           ray.origin.z + ray.dir.z * broad_t};
        out->hit = true;
        out->distance = broad_t;
        out->point_world = wp;
        out->point_local = sk_mat4_mul_point(sk_mat4_inverse(model), wp);
    }
    out->handle = drawable;
    return true;
}

/* ---- scene store ------------------------------------------------------- */

static sk_scene_t *resolve(sk_handle_t scene)
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

static int find_entry(sk_scene_t *scene_ptr, sk_handle_t drawable)
{
    for (int i = 0; i < scene_ptr->count; i++) {
        if (scene_ptr->items[i].drawable == drawable) {
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
    sk_scenes[index] = (sk_scene_t){.tonemap = SK_TONEMAP_NEUTRAL};
    return handle;
}

SK_KEEP
void sk_scene_destroy(sk_handle_t scene)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return;
    }
    free(scene_ptr->items);
    sk_environment_release(scene_ptr->environment); /* no-op for 0 */
    sk_environment_release(scene_ptr->background);
    *scene_ptr = (sk_scene_t){0};
    sk_handle_pool_free(&sk_scene_pool, scene);
}

SK_KEEP
bool sk_scene_add(sk_handle_t scene, sk_handle_t drawable, int layer)
{
    sk_scene_t *scene_ptr = resolve(scene);
    int existing;

    if (scene_ptr == NULL || drawable == 0) {
        return false;
    }

    existing = find_entry(scene_ptr, drawable);
    if (existing >= 0) {
        scene_ptr->items[existing].layer = layer;
        return true;
    }

    if (scene_ptr->count >= scene_ptr->capacity) {
        int cap = scene_ptr->capacity > 0 ? scene_ptr->capacity * 2 : 8;
        sk_scene_entry_t *grown = realloc(scene_ptr->items, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        scene_ptr->items = grown;
        scene_ptr->capacity = cap;
    }

    scene_ptr->items[scene_ptr->count].drawable = drawable;
    scene_ptr->items[scene_ptr->count].layer = layer;
    scene_ptr->count++;
    return true;
}

SK_KEEP
bool sk_scene_set_layer(sk_handle_t scene, sk_handle_t drawable, int layer)
{
    sk_scene_t *scene_ptr = resolve(scene);
    int idx;
    if (scene_ptr == NULL) {
        return false;
    }
    idx = find_entry(scene_ptr, drawable);
    if (idx < 0) {
        return false;
    }
    scene_ptr->items[idx].layer = layer;
    return true;
}

SK_KEEP
bool sk_scene_remove(sk_handle_t scene, sk_handle_t drawable)
{
    sk_scene_t *scene_ptr = resolve(scene);
    int idx;
    if (scene_ptr == NULL) {
        return false;
    }
    idx = find_entry(scene_ptr, drawable);
    if (idx < 0) {
        return false;
    }
    /* preserve order (stable for equal layers) */
    memmove(&scene_ptr->items[idx], &scene_ptr->items[idx + 1],
            (size_t)(scene_ptr->count - idx - 1) * sizeof(sk_scene_entry_t));
    scene_ptr->count--;
    return true;
}

SK_KEEP
void sk_scene_clear(sk_handle_t scene)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return;
    }
    scene_ptr->count = 0;
}

SK_KEEP
void sk_scene_set_active_camera(sk_handle_t scene, sk_handle_t camera)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return;
    }
    scene_ptr->camera = camera;
}

SK_KEEP
bool sk_scene_set_ambient(sk_handle_t scene, sk_handle_t color, float intensity)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return false;
    }
    scene_ptr->ambient_color = color;
    scene_ptr->ambient_intensity = intensity > 0.0f ? intensity : 0.0f;
    return true;
}

SK_KEEP
bool sk_scene_set_environment(sk_handle_t scene, sk_handle_t environment, float intensity, float rotation)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL || (environment != 0 && sk_handle_get_kind(environment) != SK_HANDLE_KIND_ENVIRONMENT)) {
        return false;
    }
    sk_environment_retain(environment); /* no-op for 0 */
    sk_environment_release(scene_ptr->environment);
    scene_ptr->environment = environment;
    scene_ptr->environment_intensity = intensity > 0.0f ? intensity : 0.0f;
    scene_ptr->environment_rotation = rotation;
    return true;
}

SK_KEEP
bool sk_scene_set_background(sk_handle_t scene, sk_handle_t environment, float blur)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL || (environment != 0 && sk_handle_get_kind(environment) != SK_HANDLE_KIND_ENVIRONMENT)) {
        return false;
    }
    sk_environment_retain(environment);
    sk_environment_release(scene_ptr->background);
    scene_ptr->background = environment;
    scene_ptr->background_blur = blur < 0.0f ? 0.0f : (blur > 1.0f ? 1.0f : blur);
    return true;
}

SK_KEEP
bool sk_scene_set_tonemap(sk_handle_t scene, sk_tonemap_t tonemap, float exposure)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL || tonemap < SK_TONEMAP_NONE || tonemap > SK_TONEMAP_ACES) {
        return false;
    }
    scene_ptr->tonemap = tonemap;
    scene_ptr->exposure = exposure;
    return true;
}

/* Gather the scene's enabled lights (in member order) and ambient into a
 * lighting environment for the models drawn by this scene draw. */
static int push_lighting(const sk_scene_t *scene_ptr)
{
    static sk_light_env_t env; /* large; built and copied once per scene draw */
    color_t ambient = sk_color_get(scene_ptr->ambient_color);

    env.count = 0;
    env.environment = scene_ptr->environment;
    env.environment_intensity = scene_ptr->environment_intensity;
    env.environment_rotation = scene_ptr->environment_rotation;
    env.tonemap = (int)scene_ptr->tonemap;
    env.exposure = scene_ptr->exposure;
    env.ambient = (vec3_t){sk_srgb_to_linear(ambient.r) * scene_ptr->ambient_intensity,
                           sk_srgb_to_linear(ambient.g) * scene_ptr->ambient_intensity,
                           sk_srgb_to_linear(ambient.b) * scene_ptr->ambient_intensity};
    for (int i = 0; i < scene_ptr->count && env.count < SK_MAX_SCENE_LIGHTS; i++) {
        if (sk_handle_get_kind(scene_ptr->items[i].drawable) == SK_HANDLE_KIND_LIGHT &&
            sk_light_get_scene_light(scene_ptr->items[i].drawable, &env.lights[env.count])) {
            env.count++;
        }
    }
    return sk_light_env_push(&env);
}

/* Stable insertion sort by layer, ascending (member order breaks ties). */
static void sort_by_layer(sk_scene_t *scene_ptr)
{
    for (int i = 1; i < scene_ptr->count; i++) {
        sk_scene_entry_t key = scene_ptr->items[i];
        int j = i - 1;
        while (j >= 0 && scene_ptr->items[j].layer > key.layer) {
            scene_ptr->items[j + 1] = scene_ptr->items[j];
            j--;
        }
        scene_ptr->items[j + 1] = key;
    }
}

/* One layer: opaque parts first, then transparent parts sorted back to front
 * across all drawable kinds. Consecutive parts of the same kind stay batched
 * (the render command list merges adjacent sokol_gl and model runs). */
static void draw_layer(const sk_scene_entry_t *entries, int count, const sk_camera3d_t *cam)
{
    int transparent_count = 0;

    for (int i = 0; i < count; i++) {
        const sk_drawable_passes_t *passes = lookup_passes(entries[i].drawable);
        if (passes != NULL && passes->draw_opaque != NULL) {
            passes->draw_opaque(entries[i].drawable);
        }
    }

    for (int i = 0; i < count; i++) {
        const sk_drawable_passes_t *passes = lookup_passes(entries[i].drawable);
        int room = MAX_TRANSPARENT_ITEMS - transparent_count;
        int first = transparent_count;
        if (passes == NULL || passes->collect_transparent == NULL) {
            continue;
        }
        if (room <= 0) {
            if (!sk_transparent_overflow_logged) {
                log_warn("scene: MAX_TRANSPARENT_ITEMS (%d) reached; skipping transparent parts",
                         MAX_TRANSPARENT_ITEMS);
                sk_transparent_overflow_logged = true;
            }
            break;
        }
        transparent_count += passes->collect_transparent(entries[i].drawable, cam,
                                                         &sk_transparent_items[first], room);
        for (int t = first; t < transparent_count; t++) {
            sk_transparent_items[t].order = t;
        }
    }

    if (transparent_count == 0) {
        return;
    }
    sk_scene_sort_transparent(sk_transparent_items, transparent_count);
    sk_render_set_3d_transparent(true);
    for (int t = 0; t < transparent_count; t++) {
        const sk_transparent_item_t *item = &sk_transparent_items[t];
        const sk_drawable_passes_t *passes = lookup_passes(item->handle);
        if (passes != NULL && passes->draw_transparent != NULL) {
            passes->draw_transparent(item->handle, item->part);
        }
    }
    sk_render_set_3d_transparent(false);
}

SK_KEEP
void sk_scene_draw(sk_handle_t scene)
{
    sk_scene_t *scene_ptr = resolve(scene);
    sk_camera3d_t cam;
    if (scene_ptr == NULL) {
        return;
    }

    if (scene_ptr->camera != 0) {
        sk_camera3d_set_active(scene_ptr->camera);
    }

    sort_by_layer(scene_ptr);

    if (!sk_camera3d_get_active_data(&cam)) {
        return;
    }

    sk_light_env_set_current(push_lighting(scene_ptr));
    if (scene_ptr->background != 0) {
        const bool same = scene_ptr->background == scene_ptr->environment;
        sk_environment_submit_background(scene_ptr->background, scene_ptr->background_blur,
                                         same ? scene_ptr->environment_intensity : 1.0f,
                                         same ? scene_ptr->environment_rotation : 0.0f,
                                         (int)scene_ptr->tonemap, scene_ptr->exposure);
    }
    sk_render_begin_mode_3d();
    for (int start = 0; start < scene_ptr->count;) {
        int layer = scene_ptr->items[start].layer;
        int end = start;
        while (end < scene_ptr->count && scene_ptr->items[end].layer == layer) {
            end++;
        }
        draw_layer(&scene_ptr->items[start], end - start, &cam);
        start = end;
    }
    sk_render_end_mode_3d();
    sk_light_env_set_current(-1); /* models drawn outside a scene are unlit */

    /* 2D members on top of all 3D, in layer then member order */
    for (int i = 0; i < scene_ptr->count; i++) {
        const sk_drawable_passes_t *passes = lookup_passes(scene_ptr->items[i].drawable);
        if (passes != NULL && passes->draw_2d != NULL) {
            passes->draw_2d(scene_ptr->items[i].drawable);
        }
    }
}

/* ---- picking ----------------------------------------------------------- */

SK_KEEP
sk_pick_result_t sk_scene_pick(sk_handle_t scene, sk_handle_t camera,
                               float mouse_x, float mouse_y)
{
    sk_pick_result_t result = {0};
    sk_scene_t *scene_ptr = resolve(scene);
    sk_camera3d_t cam;
    sk_ray_t ray;
    vec2_t screen;
    float best_t = 1e30f;

    if (scene_ptr == NULL) {
        return result;
    }

    /* 2D members are drawn on top of 3D, so they're hit first: topmost (last
     * drawn) first */
    sort_by_layer(scene_ptr);
    for (int i = scene_ptr->count - 1; i >= 0; i--) {
        const sk_drawable_passes_t *passes = lookup_passes(scene_ptr->items[i].drawable);
        if (passes != NULL && passes->pick_2d != NULL &&
            pick_2d(scene_ptr->items[i].drawable, passes, mouse_x, mouse_y, &result)) {
            return result;
        }
    }
    result = (sk_pick_result_t){0};

    if (camera == 0) {
        camera = scene_ptr->camera;
    }
    if (camera != 0) {
        sk_camera3d_set_active(camera);
    }
    if (!sk_camera3d_get_active_data(&cam)) {
        return result;
    }

    screen = sk_window_get_screen_size();
    ray = sk_pick_ray_from_screen(&cam, mouse_x, mouse_y, screen.x, screen.y);

    for (int i = 0; i < scene_ptr->count; i++) {
        sk_pick_result_t hit = {0};
        if (pick_3d(scene_ptr->items[i].drawable, ray, &hit) && hit.distance < best_t) {
            best_t = hit.distance;
            result = hit;
        }
    }
    return result;
}

SK_KEEP
sk_pick_result_t sk_pick_object(sk_handle_t object, sk_handle_t camera, float x, float y)
{
    sk_pick_result_t result = {0};
    const sk_drawable_passes_t *passes = lookup_passes(object);
    sk_camera3d_t cam;
    vec2_t screen;

    if (object == 0) {
        return result;
    }
    if (passes != NULL && passes->pick_2d != NULL) {
        pick_2d(object, passes, x, y, &result);
        return result;
    }
    if (camera != 0) {
        sk_camera3d_set_active(camera);
    }
    if (!sk_camera3d_get_active_data(&cam)) {
        return result;
    }
    screen = sk_window_get_screen_size();
    pick_3d(object, sk_pick_ray_from_screen(&cam, x, y, screen.x, screen.y), &result);
    return result;
}

SK_KEEP
sk_pick_stats_t sk_pick_get_stats(void)
{
    return sk_pick_stats;
}

SK_KEEP
void sk_pick_reset_stats(void)
{
    sk_pick_stats = (sk_pick_stats_t){0};
}

void sk_scene_init(void)
{
    memset(sk_scenes, 0, sizeof(sk_scenes));
    memset(sk_passes_registry, 0, sizeof(sk_passes_registry));
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
        sk_scenes[i] = (sk_scene_t){0};
    }
    sk_handle_pool_reset(&sk_scene_pool);
}
