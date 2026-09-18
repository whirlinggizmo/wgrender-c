#include "sk_scene.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_color.h"
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

#define SCENES_INITIAL 8 /* slots to start with; the pool doubles as needed */
#define SK_DRAWABLE_KIND_COUNT 64 /* handle kind is 6 bits */
/* Transparent parts per scene layer: the list starts at TRANSPARENT_INITIAL and doubles
 * as needed, up to SK_MAX_TRANSPARENT_ITEMS (overridable at build time,
 * -DSK_MAX_TRANSPARENT_ITEMS=...). */
#ifndef SK_MAX_TRANSPARENT_ITEMS
#define SK_MAX_TRANSPARENT_ITEMS (1 << 20)
#endif
#define TRANSPARENT_INITIAL 1024

typedef struct {
    sk_handle_t drawable;
    int layer;
} sk_scene_entry_t;

/* Interaction edges for one context (frame or tick; see sk_input.c). A frame adds at
 * most one of each, but tick edges carry over frames that run no ticks. */
#define SK_INTERACTION_MAX_EDGES 8
typedef struct {
    sk_handle_t entered[SK_INTERACTION_MAX_EDGES];
    int entered_count;
    sk_handle_t left[SK_INTERACTION_MAX_EDGES];
    int left_count;
    sk_handle_t pressed;
    sk_handle_t released;
    sk_handle_t clicked;
} sk_interaction_edges_t;

typedef struct {
    bool interactive;
    sk_handle_t hovered;     /* topmost under the pointer (enabled or not) */
    sk_handle_t press_target; /* what the held press started on (0: nothing) */
    bool press_down;
    sk_interaction_edges_t frame_edges;
    sk_interaction_edges_t tick_edges;
} sk_interaction_t;

/* A clip rectangle for one layer's 2D members (screen pixels, top-left origin). */
#define MAX_SCENE_CLIPS 8
typedef struct {
    int layer;
    float x, y, width, height;
} sk_scene_clip_t;

typedef struct {
    sk_scene_entry_t *items; /* members, in layer order once tidy; drawable 0 = removed */
    int count;
    int capacity;
    int *index;              /* open addressing, handle -> item index + 1 (0 = empty) */
    int index_capacity;      /* a power of two, at least twice the members */
    bool dirty;              /* removed members, or layer order to restore: tidy() */
    sk_scene_clip_t clips[MAX_SCENE_CLIPS];
    int clip_count;
    sk_interaction_t interaction;
    sk_handle_t camera;
    sk_color_t ambient_color;
    float ambient_intensity;    /* 0 = no ambient (default) */
    sk_handle_t environment;    /* referenced; 0 = none */
    float environment_intensity;
    float environment_rotation;
    sk_handle_t background;     /* referenced; 0 = none */
    float background_blur;
    sk_tonemap_t tonemap;
    float exposure;
} sk_scene_t;

static sk_scene_t *sk_scenes; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_scene_pool;

typedef struct {
    sk_drawable_draw_opaque_fn draw_opaque;
    sk_drawable_collect_transparent_fn collect_transparent;
    sk_drawable_draw_transparent_fn draw_transparent;
    sk_drawable_draw_2d_fn draw_2d;
    sk_drawable_pick_2d_fn pick_2d;
} sk_drawable_passes_t;

static sk_drawable_passes_t sk_passes_registry[SK_DRAWABLE_KIND_COUNT];
static sk_transparent_item_t *sk_transparent_items;
static int sk_transparent_capacity;
static bool sk_transparent_overflow_logged;
static sk_drawable_bounds_fn sk_bounds_registry[SK_DRAWABLE_KIND_COUNT];
static sk_drawable_pick_fn sk_pick_registry[SK_DRAWABLE_KIND_COUNT];
static sk_drawable_enabled_fn sk_enabled_registry[SK_DRAWABLE_KIND_COUNT];
static bool sk_scene_capture_releasing; /* the capturing press was released last frame */

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

/* A radix key for far-to-near order: floats map to unsigned ints that sort the same
 * way (flip every bit of negatives, just the sign of the rest), then invert so the
 * farthest comes first. */
static uint32_t far_first_key(float depth)
{
    uint32_t bits;
    memcpy(&bits, &depth, sizeof(bits));
    bits = (bits & 0x80000000u) ? ~bits : (bits | 0x80000000u);
    return ~bits;
}

static sk_transparent_item_t *sk_sort_scratch;
static int sk_sort_scratch_capacity;

void sk_scene_sort_transparent(sk_transparent_item_t *items, int count)
{
    bool in_order = true;
    sk_transparent_item_t *from = items, *to;

    if (count < 2) {
        return;
    }
    for (int i = 1; i < count && in_order; i++) {
        in_order = items[i - 1].order < items[i].order;
    }
    /* few, or ties not already in submission order: a comparison sort */
    if (count < 64 || !in_order) {
        qsort(items, (size_t)count, sizeof(items[0]), compare_transparent);
        return;
    }
    if (count > sk_sort_scratch_capacity) {
        sk_transparent_item_t *grown = realloc(sk_sort_scratch, sizeof(*grown) * (size_t)count);
        if (grown == NULL) {
            qsort(items, (size_t)count, sizeof(items[0]), compare_transparent);
            return;
        }
        sk_sort_scratch = grown;
        sk_sort_scratch_capacity = count;
    }
    /* least significant byte first, 4 stable passes: ties keep submission order */
    to = sk_sort_scratch;
    for (int shift = 0; shift < 32; shift += 8) {
        int offsets[256] = {0};
        for (int i = 0; i < count; i++) offsets[(far_first_key(from[i].depth) >> shift) & 255]++;
        for (int b = 0, sum = 0; b < 256; b++) {
            const int n = offsets[b];
            offsets[b] = sum;
            sum += n;
        }
        for (int i = 0; i < count; i++) to[offsets[(far_first_key(from[i].depth) >> shift) & 255]++] = from[i];
        sk_transparent_item_t *swap = from;
        from = to;
        to = swap;
    }
    /* an even number of passes: the sorted items are back in `items` */
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

void sk_scene_register_enabled(sk_handle_kind_t kind, sk_drawable_enabled_fn enabled)
{
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT) {
        return;
    }
    sk_enabled_registry[kind] = enabled;
}

/* A 2D member: its kind draws and picks in screen space (sprite2d, text2d,
 * shape2d). Each kind is one or the other, so the handle says which. */
static bool is_2d_member(sk_handle_t handle)
{
    const sk_handle_kind_t kind = sk_handle_get_kind(handle);
    return (int)kind >= 0 && (int)kind < SK_DRAWABLE_KIND_COUNT && sk_passes_registry[kind].pick_2d != NULL;
}

/* The clip rectangle of a layer, or NULL when it isn't clipped. */
static const sk_scene_clip_t *lookup_clip(const sk_scene_t *scene_ptr, int layer)
{
    for (int i = 0; i < scene_ptr->clip_count; i++) {
        if (scene_ptr->clips[i].layer == layer) {
            return &scene_ptr->clips[i];
        }
    }
    return NULL;
}

/* True when a screen point falls outside the clip rectangle of a member's layer:
 * what isn't drawn isn't picked either. */
static bool clipped_out(const sk_scene_t *scene_ptr, int layer, float x, float y)
{
    const sk_scene_clip_t *clip = lookup_clip(scene_ptr, layer);
    return clip != NULL &&
           (x < clip->x || y < clip->y || x > clip->x + clip->width || y > clip->y + clip->height);
}

static bool is_enabled(sk_handle_t handle)
{
    const sk_handle_kind_t kind = sk_handle_get_kind(handle);
    if ((int)kind < 0 || (int)kind >= SK_DRAWABLE_KIND_COUNT || sk_enabled_registry[kind] == NULL) {
        return true;
    }
    return sk_enabled_registry[kind](handle);
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

/* Membership: a hash index from handle to item, so adding, removing, destroying and
 * relayering are constant time however many members a scene has. Removing leaves a
 * hole (drawable 0); tidy() closes holes, restores layer order and rebuilds the index
 * before the members are walked (draw, pick, interaction). */
static unsigned slot_of(const sk_scene_t *scene_ptr, sk_handle_t drawable)
{
    return (unsigned)((drawable * 2654435761u) >> 7) & (unsigned)(scene_ptr->index_capacity - 1);
}

static int find_entry(sk_scene_t *scene_ptr, sk_handle_t drawable)
{
    if (scene_ptr->index_capacity == 0 || drawable == 0) {
        return -1;
    }
    for (unsigned slot = slot_of(scene_ptr, drawable);; slot = (slot + 1) & (unsigned)(scene_ptr->index_capacity - 1)) {
        const int entry = scene_ptr->index[slot];
        if (entry == 0) return -1;
        if (scene_ptr->items[entry - 1].drawable == drawable) return entry - 1;
    }
}

static void index_insert(sk_scene_t *scene_ptr, int item)
{
    unsigned slot = slot_of(scene_ptr, scene_ptr->items[item].drawable);
    while (scene_ptr->index[slot] != 0) {
        slot = (slot + 1) & (unsigned)(scene_ptr->index_capacity - 1);
    }
    scene_ptr->index[slot] = item + 1;
}

/* Rebuild the index for the members as they are now, at a capacity for `wanted`. */
static bool rebuild_index(sk_scene_t *scene_ptr, int wanted)
{
    int capacity = scene_ptr->index_capacity > 0 ? scene_ptr->index_capacity : 16;
    while (capacity < wanted * 2) capacity *= 2;
    if (capacity != scene_ptr->index_capacity) {
        int *index = realloc(scene_ptr->index, sizeof(int) * (size_t)capacity);
        if (index == NULL) return false;
        scene_ptr->index = index;
        scene_ptr->index_capacity = capacity;
    }
    memset(scene_ptr->index, 0, sizeof(int) * (size_t)scene_ptr->index_capacity);
    for (int i = 0; i < scene_ptr->count; i++) {
        if (scene_ptr->items[i].drawable != 0) index_insert(scene_ptr, i);
    }
    return true;
}

/* Take the member at `item` out of the index (linear probing: shift back the entries
 * after it that would no longer be found) and leave a hole in the items. */
static void remove_entry(sk_scene_t *scene_ptr, int item)
{
    const unsigned mask = (unsigned)(scene_ptr->index_capacity - 1);
    unsigned hole = slot_of(scene_ptr, scene_ptr->items[item].drawable);
    while (scene_ptr->index[hole] != item + 1) hole = (hole + 1) & mask;
    for (unsigned next = (hole + 1) & mask; scene_ptr->index[next] != 0; next = (next + 1) & mask) {
        const unsigned home = slot_of(scene_ptr, scene_ptr->items[scene_ptr->index[next] - 1].drawable);
        /* move it into the hole unless its home lies cyclically in (hole, next] */
        if (((next - home) & mask) >= ((next - hole) & mask)) {
            scene_ptr->index[hole] = scene_ptr->index[next];
            hole = next;
        }
    }
    scene_ptr->index[hole] = 0;
    scene_ptr->items[item].drawable = 0;
    scene_ptr->dirty = true;
}

/* Close the holes, put the members in layer order (stable) and rebuild the index. */
static void tidy(sk_scene_t *scene_ptr)
{
    int kept = 0;
    if (!scene_ptr->dirty) {
        return;
    }
    for (int i = 0; i < scene_ptr->count; i++) {
        if (scene_ptr->items[i].drawable != 0) scene_ptr->items[kept++] = scene_ptr->items[i];
    }
    scene_ptr->count = kept;
    for (int i = 1; i < scene_ptr->count; i++) {
        sk_scene_entry_t key = scene_ptr->items[i];
        int j = i - 1;
        while (j >= 0 && scene_ptr->items[j].layer > key.layer) {
            scene_ptr->items[j + 1] = scene_ptr->items[j];
            j--;
        }
        scene_ptr->items[j + 1] = key;
    }
    rebuild_index(scene_ptr, scene_ptr->count);
    scene_ptr->dirty = false;
}

SK_KEEP
sk_handle_t sk_scene_create(void)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_scene_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("scene: pool full (%u)", (unsigned)sk_scene_pool.max - 1u);
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
    free(scene_ptr->index);
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
        scene_ptr->dirty = true;
        return true;
    }
    if ((scene_ptr->count + 1) * 2 > scene_ptr->index_capacity && !rebuild_index(scene_ptr, scene_ptr->count + 1)) {
        return false;
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
    index_insert(scene_ptr, scene_ptr->count);
    scene_ptr->count++;
    scene_ptr->dirty = true; /* its layer may not be the last one */
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
    scene_ptr->dirty = true;
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
    remove_entry(scene_ptr, idx); /* the rest keep their order */
    return true;
}

void sk_scene_forget(sk_handle_t object)
{
    if (object == 0) {
        return;
    }
    for (uint16_t i = 1; i < sk_scene_pool.capacity; i++) {
        sk_scene_t *scene_ptr = &sk_scenes[i];
        int idx;
        if (!sk_scene_pool.occupied[i]) {
            continue;
        }
        if ((idx = find_entry(scene_ptr, object)) >= 0) {
            remove_entry(scene_ptr, idx);
        }
        if (scene_ptr->interaction.hovered == object) {
            scene_ptr->interaction.hovered = 0;
        }
        if (scene_ptr->interaction.press_target == object) {
            scene_ptr->interaction.press_target = 0;
        }
        if (scene_ptr->camera == object) {
            scene_ptr->camera = 0;
        }
    }
}

SK_KEEP
void sk_scene_clear(sk_handle_t scene)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return;
    }
    scene_ptr->count = 0;
    scene_ptr->dirty = false;
    if (scene_ptr->index != NULL) {
        memset(scene_ptr->index, 0, sizeof(int) * (size_t)scene_ptr->index_capacity);
    }
}

SK_KEEP
bool sk_scene_set_clip(sk_handle_t scene, int layer, float x, float y, float width, float height)
{
    sk_scene_t *scene_ptr = resolve(scene);
    int slot = -1;

    if (scene_ptr == NULL) {
        return false;
    }
    for (int i = 0; i < scene_ptr->clip_count; i++) {
        if (scene_ptr->clips[i].layer == layer) {
            slot = i;
            break;
        }
    }
    if (width <= 0.0f || height <= 0.0f) { /* no clip: drop the layer's rectangle */
        if (slot >= 0) {
            scene_ptr->clips[slot] = scene_ptr->clips[--scene_ptr->clip_count];
        }
        return true;
    }
    if (slot < 0) {
        if (scene_ptr->clip_count >= MAX_SCENE_CLIPS) {
            log_warn("sk_scene_set_clip: at most %d clipped layers per scene", MAX_SCENE_CLIPS);
            return false;
        }
        slot = scene_ptr->clip_count++;
    }
    scene_ptr->clips[slot] = (sk_scene_clip_t){.layer = layer, .x = x, .y = y, .width = width, .height = height};
    return true;
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
bool sk_scene_set_ambient(sk_handle_t scene, sk_color_t color, float intensity)
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
    sk_colorf_t ambient = sk_color_unpack(scene_ptr->ambient_color);

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

/* One layer: opaque parts first, then transparent parts sorted back to front
 * across all drawable kinds. Consecutive parts of the same kind stay batched
 * (the render command list merges adjacent sokol_gl and model runs). */

/* Double the transparent list, up to SK_MAX_TRANSPARENT_ITEMS; false when it can't. */
static bool grow_transparent_items(void)
{
    const int capacity = sk_transparent_capacity == 0 ? TRANSPARENT_INITIAL : sk_transparent_capacity * 2;
    sk_transparent_item_t *items;
    if (sk_transparent_capacity >= SK_MAX_TRANSPARENT_ITEMS) {
        return false;
    }
    const int size = capacity < SK_MAX_TRANSPARENT_ITEMS ? capacity : SK_MAX_TRANSPARENT_ITEMS;
    items = realloc(sk_transparent_items, sizeof(*items) * (size_t)size);
    if (items == NULL) {
        return false;
    }
    sk_transparent_items = items;
    sk_transparent_capacity = size;
    log_debug("scene: transparent list grown to %d parts", sk_transparent_capacity);
    return true;
}

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
        int first = transparent_count;
        int room, collected;
        if (passes == NULL || passes->collect_transparent == NULL) {
            continue;
        }
        /* a drawable that fills the room left may have had more: grow and collect it again */
        for (;;) {
            room = sk_transparent_capacity - transparent_count;
            collected = room > 0 ? passes->collect_transparent(entries[i].drawable, cam,
                                                               &sk_transparent_items[first], room)
                                 : 0;
            if (collected < room || !grow_transparent_items()) {
                break;
            }
        }
        if (room <= 0 || collected >= room) {
            if (!sk_transparent_overflow_logged) {
                log_warn("scene: %d transparent parts in one layer, the most there can be; skipping the rest",
                         SK_MAX_TRANSPARENT_ITEMS);
                sk_transparent_overflow_logged = true;
            }
        }
        transparent_count += collected;
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

    tidy(scene_ptr);

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

    /* 2D members on top of all 3D, in layer then member order, each layer inside
     * its clip rectangle (sk_scene_set_clip) if it has one */
    {
        const sk_scene_clip_t *clip = NULL;
        int clip_layer = 0;
        bool layer_known = false;
        for (int i = 0; i < scene_ptr->count; i++) {
            const sk_handle_t drawable = scene_ptr->items[i].drawable;
            const sk_drawable_passes_t *passes = lookup_passes(drawable);
            if (passes == NULL || passes->draw_2d == NULL || !is_2d_member(drawable)) {
                continue;
            }
            if (!layer_known || scene_ptr->items[i].layer != clip_layer) {
                const sk_scene_clip_t *next = lookup_clip(scene_ptr, scene_ptr->items[i].layer);
                if (clip != NULL) {
                    sk_render_pop_clip(); /* leaving the previous layer's clip */
                }
                if (next != NULL) {
                    sk_render_push_clip(next->x, next->y, next->width, next->height);
                }
                clip = next;
                clip_layer = scene_ptr->items[i].layer;
                layer_known = true;
            }
            passes->draw_2d(drawable);
        }
        if (clip != NULL) {
            sk_render_pop_clip();
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
    tidy(scene_ptr);
    for (int i = scene_ptr->count - 1; i >= 0; i--) {
        const sk_drawable_passes_t *passes = lookup_passes(scene_ptr->items[i].drawable);
        if (is_2d_member(scene_ptr->items[i].drawable) &&
            !clipped_out(scene_ptr, scene_ptr->items[i].layer, mouse_x, mouse_y) &&
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
        if (!is_2d_member(scene_ptr->items[i].drawable) && pick_3d(scene_ptr->items[i].drawable, ray, &hit) &&
            hit.distance < best_t) {
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
    if (is_2d_member(object)) {
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

/* ---- pointer interaction ---------------------------------------------- */

/* The topmost pickable member under a screen point: 2D members first (last drawn
 * first), then the nearest 3D hit through the scene's camera. Unlike sk_scene_pick it
 * doesn't change the active camera or count pick statistics. */
static sk_handle_t pick_member(sk_scene_t *scene_ptr, float x, float y, bool *is_2d)
{
    const sk_pick_stats_t stats = sk_pick_stats;
    sk_pick_result_t result = {0};
    sk_handle_t hit = 0;
    float best_t = 1e30f;
    sk_camera3d_t cam;

    *is_2d = false;
    tidy(scene_ptr);
    for (int i = scene_ptr->count - 1; i >= 0 && hit == 0; i--) {
        const sk_drawable_passes_t *passes = lookup_passes(scene_ptr->items[i].drawable);
        if (is_2d_member(scene_ptr->items[i].drawable) &&
            !clipped_out(scene_ptr, scene_ptr->items[i].layer, x, y) &&
            pick_2d(scene_ptr->items[i].drawable, passes, x, y, &result)) {
            hit = scene_ptr->items[i].drawable;
            *is_2d = true;
        }
    }
    if (hit == 0 && sk_camera3d_get_data(scene_ptr->camera, &cam)) {
        const vec2_t screen = sk_window_get_screen_size();
        const sk_ray_t ray = sk_pick_ray_from_screen(&cam, x, y, screen.x, screen.y);
        for (int i = 0; i < scene_ptr->count; i++) {
            sk_pick_result_t candidate = {0};
            if (!is_2d_member(scene_ptr->items[i].drawable) && pick_3d(scene_ptr->items[i].drawable, ray, &candidate) &&
                candidate.distance < best_t) {
                best_t = candidate.distance;
                hit = scene_ptr->items[i].drawable;
            }
        }
    }
    sk_pick_stats = stats;
    return hit;
}

static void add_edge(sk_handle_t *list, int *count, sk_handle_t handle)
{
    if (*count < SK_INTERACTION_MAX_EDGES) {
        list[(*count)++] = handle;
    }
}

static void add_interaction_edges(sk_interaction_t *state, sk_handle_t entered, sk_handle_t left, sk_handle_t pressed,
                                  sk_handle_t released, sk_handle_t clicked)
{
    sk_interaction_edges_t *sets[2] = {&state->frame_edges, &state->tick_edges};
    for (int i = 0; i < 2; i++) {
        if (entered != 0) add_edge(sets[i]->entered, &sets[i]->entered_count, entered);
        if (left != 0) add_edge(sets[i]->left, &sets[i]->left_count, left);
        if (pressed != 0) sets[i]->pressed = pressed;
        if (released != 0) sets[i]->released = released;
        if (clicked != 0) sets[i]->clicked = clicked;
    }
}

void sk_scene_update_interaction(void)
{
    float x, y;
    bool down, pressed, released;
    bool captured = false;

    sk_input_get_pointer_frame(&x, &y, &down, &pressed, &released);
    if (sk_scene_capture_releasing) {
        sk_input_set_scene_pointer_captured(false); /* captured through the release frame */
        sk_scene_capture_releasing = false;
    }
    for (int i = 0; i < sk_scene_pool.capacity; i++) {
        sk_scene_t *scene_ptr = &sk_scenes[i];
        sk_interaction_t *state = &scene_ptr->interaction;
        sk_handle_t entered = 0, left = 0, pressed_on = 0, released_on = 0, clicked_on = 0;
        bool is_2d = false;
        sk_handle_t hit;

        if (!sk_scene_pool.occupied[i] || !state->interactive) {
            continue;
        }
        hit = pick_member(scene_ptr, x, y, &is_2d);
        if (hit != state->hovered) {
            left = state->hovered;
            entered = hit;
            state->hovered = hit;
        }
        if (pressed) {
            state->press_target = hit; /* a disabled hit still takes the press: it blocks */
            state->press_down = true;
            pressed_on = hit != 0 && is_enabled(hit) ? hit : 0;
            captured = captured || (hit != 0 && is_2d);
        }
        if (released && state->press_down) {
            state->press_down = false;
            if (state->press_target != 0 && is_enabled(state->press_target)) {
                released_on = state->press_target;
                clicked_on = hit == state->press_target ? hit : 0;
            }
        }
        add_interaction_edges(state, entered, left, pressed_on, released_on, clicked_on);
    }
    if (captured) {
        sk_input_set_scene_pointer_captured(true);
    }
    if (released) {
        sk_scene_capture_releasing = true; /* clears next frame */
    }
}

void sk_scene_end_tick_interaction(void)
{
    for (int i = 0; i < sk_scene_pool.capacity; i++) {
        sk_scenes[i].interaction.tick_edges = (sk_interaction_edges_t){0};
    }
}

void sk_scene_end_frame_interaction(void)
{
    for (int i = 0; i < sk_scene_pool.capacity; i++) {
        sk_scenes[i].interaction.frame_edges = (sk_interaction_edges_t){0};
    }
}

static const sk_interaction_edges_t *current_interaction_edges(const sk_scene_t *scene_ptr)
{
    return sk_input_get_context() == SK_INPUT_CONTEXT_TICK ? &scene_ptr->interaction.tick_edges
                                                            : &scene_ptr->interaction.frame_edges;
}

static bool contains(const sk_handle_t *list, int count, sk_handle_t handle)
{
    for (int i = 0; i < count; i++) {
        if (list[i] == handle) return true;
    }
    return false;
}

SK_KEEP
bool sk_scene_set_interactive(sk_handle_t scene, bool interactive)
{
    sk_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return false;
    }
    scene_ptr->interaction = (sk_interaction_t){.interactive = interactive};
    return true;
}

SK_KEEP
bool sk_scene_is_interactive(sk_handle_t scene)
{
    const sk_scene_t *scene_ptr = resolve(scene);
    return scene_ptr != NULL && scene_ptr->interaction.interactive;
}

SK_KEEP
sk_handle_t sk_scene_get_hovered(sk_handle_t scene)
{
    const sk_scene_t *scene_ptr = resolve(scene);
    return scene_ptr != NULL ? scene_ptr->interaction.hovered : 0;
}

SK_KEEP
sk_button_state_t sk_scene_get_hover(sk_handle_t scene, sk_handle_t object)
{
    const sk_scene_t *scene_ptr = resolve(scene);
    const sk_interaction_edges_t *edges;
    if (scene_ptr == NULL || object == 0 || !is_enabled(object)) {
        return SK_BUTTON_UP;
    }
    edges = current_interaction_edges(scene_ptr);
    if (contains(edges->entered, edges->entered_count, object)) return SK_BUTTON_PRESSED;
    if (contains(edges->left, edges->left_count, object)) return SK_BUTTON_RELEASED;
    return scene_ptr->interaction.hovered == object ? SK_BUTTON_DOWN : SK_BUTTON_UP;
}

SK_KEEP
sk_button_state_t sk_scene_get_press(sk_handle_t scene, sk_handle_t object)
{
    const sk_scene_t *scene_ptr = resolve(scene);
    const sk_interaction_edges_t *edges;
    if (scene_ptr == NULL || object == 0 || !is_enabled(object)) {
        return SK_BUTTON_UP;
    }
    edges = current_interaction_edges(scene_ptr);
    if (edges->pressed == object) return SK_BUTTON_PRESSED;
    if (edges->released == object) return SK_BUTTON_RELEASED;
    return scene_ptr->interaction.press_down && scene_ptr->interaction.press_target == object ? SK_BUTTON_DOWN
                                                                                               : SK_BUTTON_UP;
}

SK_KEEP
bool sk_scene_is_clicked(sk_handle_t scene, sk_handle_t object)
{
    const sk_scene_t *scene_ptr = resolve(scene);
    return scene_ptr != NULL && object != 0 && is_enabled(object) &&
           current_interaction_edges(scene_ptr)->clicked == object;
}

void sk_scene_init(void)
{
    memset(sk_passes_registry, 0, sizeof(sk_passes_registry));
    memset(sk_bounds_registry, 0, sizeof(sk_bounds_registry));
    memset(sk_pick_registry, 0, sizeof(sk_pick_registry));
    memset(sk_enabled_registry, 0, sizeof(sk_enabled_registry));
    sk_scene_capture_releasing = false;
    if (!sk_handle_pool_init(&sk_scene_pool, SK_HANDLE_KIND_SCENE, "scene", (void **)&sk_scenes,
                             sizeof(sk_scene_t), SCENES_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("scene: out of memory");
    }
}

void sk_scene_deinit(void)
{
    free(sk_sort_scratch);
    sk_sort_scratch = NULL;
    sk_sort_scratch_capacity = 0;
    free(sk_transparent_items);
    sk_transparent_items = NULL;
    sk_transparent_capacity = 0;
    for (int i = 0; i < sk_scene_pool.capacity; i++) {
        free(sk_scenes[i].items);
        free(sk_scenes[i].index);
        sk_scenes[i] = (sk_scene_t){0};
    }
    sk_handle_pool_destroy(&sk_scene_pool);
}
