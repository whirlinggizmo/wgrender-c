#include "wgr_scene.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/wgr_color.h"
#include "internal/wgr_camera3d.h"
#include "internal/wgr_handle_pool.h"
#include "internal/wgr_internal.h"
#include "internal/wgr_math.h"
#include "internal/wgr_scene.h"
#include "internal/wgr_pick.h"
#include "wgr_light.h" /* light types, for what a caster's shadow can reach */
#include "internal/wgr_render.h"
#include "wgr_camera3d.h"
#include "wgr_pick.h"
#include "wgr_logger.h"
#include "wgr_render.h"
#include "wgr_window.h"

wgr_scene_hooks_t wgr_scene_hooks;

/* Environments and lights are optional modules (internal/wgr_module.h): a scene reaches
 * them through its hooks, and does without when they aren't linked. */
static void retain_environment(wgr_handle_t environment)
{
    if (environment != 0 && wgr_scene_hooks.environment_retain != NULL) wgr_scene_hooks.environment_retain(environment);
}

static void release_environment(wgr_handle_t environment)
{
    if (environment != 0 && wgr_scene_hooks.environment_release != NULL) wgr_scene_hooks.environment_release(environment);
}

static void begin_unordered(void)
{
    if (wgr_scene_hooks.sprites_begin_unordered != NULL) wgr_scene_hooks.sprites_begin_unordered();
    if (wgr_scene_hooks.models_begin_unordered != NULL) wgr_scene_hooks.models_begin_unordered();
}

static void end_unordered(void)
{
    if (wgr_scene_hooks.sprites_end_unordered != NULL) wgr_scene_hooks.sprites_end_unordered();
    if (wgr_scene_hooks.models_end_unordered != NULL) wgr_scene_hooks.models_end_unordered();
}

#define SCENES_INITIAL 8 /* slots to start with; the pool doubles as needed */
#define WGR_DRAWABLE_KIND_COUNT 64 /* handle kind is 6 bits */
/* Transparent parts per scene layer: the list starts at TRANSPARENT_INITIAL and doubles
 * as needed, up to WGR_MAX_TRANSPARENT_ITEMS (overridable at build time,
 * -DWGR_MAX_TRANSPARENT_ITEMS=...). */
#ifndef WGR_MAX_TRANSPARENT_ITEMS
#define WGR_MAX_TRANSPARENT_ITEMS (1 << 20)
#endif
#define TRANSPARENT_INITIAL 1024

typedef struct {
    wgr_handle_t drawable;
    int layer;
} wgr_scene_entry_t;

/* Interaction edges for one context (frame or tick; see wgr_input.c). A frame adds at
 * most one of each, but tick edges carry over frames that run no ticks. */
#define WGR_INTERACTION_MAX_EDGES 8
typedef struct {
    wgr_handle_t entered[WGR_INTERACTION_MAX_EDGES];
    int entered_count;
    wgr_handle_t left[WGR_INTERACTION_MAX_EDGES];
    int left_count;
    wgr_handle_t pressed;
    wgr_handle_t released;
    wgr_handle_t clicked;
} wgr_interaction_edges_t;

typedef struct {
    bool interactive;
    wgr_handle_t hovered;     /* topmost under the pointer (enabled or not) */
    wgr_handle_t press_target; /* what the held press started on (0: nothing) */
    bool press_down;
    wgr_interaction_edges_t frame_edges;
    wgr_interaction_edges_t tick_edges;
} wgr_interaction_t;

/* A clip rectangle for one layer's 2D members (screen pixels, top-left origin). */
#define MAX_SCENE_CLIPS 8
typedef struct {
    int layer;
    float x, y, width, height;
} wgr_scene_clip_t;

typedef struct {
    wgr_scene_entry_t *items; /* members, in layer order once tidy; drawable 0 = removed */
    int count;
    int capacity;
    int *index;              /* open addressing, handle -> item index + 1 (0 = empty) */
    int index_capacity;      /* a power of two, at least twice the members */
    bool dirty;              /* removed members, or layer order to restore: tidy() */
    wgr_scene_clip_t clips[MAX_SCENE_CLIPS];
    int clip_count;
    wgr_interaction_t interaction;
    wgr_handle_t camera;
    wgr_color_t ambient_color;
    float ambient_intensity;    /* 0 = no ambient (default) */
    wgr_handle_t environment;    /* referenced; 0 = none */
    float environment_intensity;
    float environment_rotation;
    wgr_handle_t background;     /* referenced; 0 = none */
    float background_blur;
    wgr_tonemap_t tonemap;
    float exposure;
    bool culling; /* skip members the camera can't see (and whose shadows can't reach it) */
} wgr_scene_t;

static wgr_scene_t *wgr_scenes; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_scene_pool;

typedef struct {
    wgr_drawable_draw_opaque_fn draw_opaque;
    wgr_drawable_collect_transparent_fn collect_transparent;
    wgr_drawable_draw_transparent_fn draw_transparent;
    wgr_drawable_draw_opaque_fn draw_additive;
    wgr_drawable_draw_2d_fn draw_2d;
    wgr_drawable_pick_2d_fn pick_2d;
} wgr_drawable_passes_t;

static wgr_drawable_passes_t wgr_passes_registry[WGR_DRAWABLE_KIND_COUNT];
static wgr_transparent_item_t *wgr_transparent_items;
static int wgr_transparent_capacity;
static bool wgr_transparent_overflow_logged;
static wgr_drawable_bounds_fn wgr_bounds_registry[WGR_DRAWABLE_KIND_COUNT];
static wgr_drawable_pick_fn wgr_pick_registry[WGR_DRAWABLE_KIND_COUNT];
static wgr_drawable_enabled_fn wgr_enabled_registry[WGR_DRAWABLE_KIND_COUNT];
static bool wgr_scene_capture_releasing; /* the capturing press was released last frame */

/* ---- drawable dispatch registry --------------------------------------- */

void wgr_scene_register_passes(wgr_handle_kind_t kind,
                              wgr_drawable_draw_opaque_fn draw_opaque,
                              wgr_drawable_collect_transparent_fn collect_transparent,
                              wgr_drawable_draw_transparent_fn draw_transparent)
{
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT) {
        return;
    }
    wgr_passes_registry[kind].draw_opaque = draw_opaque;
    wgr_passes_registry[kind].collect_transparent = collect_transparent;
    wgr_passes_registry[kind].draw_transparent = draw_transparent;
}

void wgr_scene_register_additive(wgr_handle_kind_t kind, wgr_drawable_draw_opaque_fn draw_additive)
{
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT) {
        return;
    }
    wgr_passes_registry[kind].draw_additive = draw_additive;
}

void wgr_scene_register_2d(wgr_handle_kind_t kind, wgr_drawable_draw_2d_fn draw, wgr_drawable_pick_2d_fn pick)
{
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT) {
        return;
    }
    wgr_passes_registry[kind].draw_2d = draw;
    wgr_passes_registry[kind].pick_2d = pick;
}

static const wgr_drawable_passes_t *lookup_passes(wgr_handle_t handle)
{
    wgr_handle_kind_t kind = wgr_handle_get_kind(handle);
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT) {
        return NULL;
    }
    return &wgr_passes_registry[kind];
}

float wgr_scene_view_depth(const wgr_camera3d_t *cam, vec3_t world_point)
{
    vec3_t forward = wgr_v3_norm(wgr_v3_sub(cam->target, cam->position));
    vec3_t offset = wgr_v3_sub(world_point, cam->position);
    return offset.x * forward.x + offset.y * forward.y + offset.z * forward.z;
}

static int compare_transparent(const void *lhs, const void *rhs)
{
    const wgr_transparent_item_t *a = (const wgr_transparent_item_t *)lhs;
    const wgr_transparent_item_t *b = (const wgr_transparent_item_t *)rhs;
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

static wgr_transparent_item_t *wgr_sort_scratch;
static int wgr_sort_scratch_capacity;

void wgr_scene_sort_transparent(wgr_transparent_item_t *items, int count)
{
    bool in_order = true;
    wgr_transparent_item_t *from = items, *to;

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
    if (count > wgr_sort_scratch_capacity) {
        wgr_transparent_item_t *grown = realloc(wgr_sort_scratch, sizeof(*grown) * (size_t)count);
        if (grown == NULL) {
            qsort(items, (size_t)count, sizeof(items[0]), compare_transparent);
            return;
        }
        wgr_sort_scratch = grown;
        wgr_sort_scratch_capacity = count;
    }
    /* least significant byte first, 4 stable passes: ties keep submission order */
    to = wgr_sort_scratch;
    for (int shift = 0; shift < 32; shift += 8) {
        int offsets[256] = {0};
        for (int i = 0; i < count; i++) offsets[(far_first_key(from[i].depth) >> shift) & 255]++;
        for (int b = 0, sum = 0; b < 256; b++) {
            const int n = offsets[b];
            offsets[b] = sum;
            sum += n;
        }
        for (int i = 0; i < count; i++) to[offsets[(far_first_key(from[i].depth) >> shift) & 255]++] = from[i];
        wgr_transparent_item_t *swap = from;
        from = to;
        to = swap;
    }
    /* an even number of passes: the sorted items are back in `items` */
}

static wgr_drawable_bounds_fn wgr_cull_bounds_registry[WGR_DRAWABLE_KIND_COUNT];
static wgr_drawable_casts_shadow_fn wgr_casts_shadow_registry[WGR_DRAWABLE_KIND_COUNT];

void wgr_scene_register_cull_bounds(wgr_handle_kind_t kind, wgr_drawable_bounds_fn bounds)
{
    if ((int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT) {
        wgr_cull_bounds_registry[kind] = bounds;
    }
}

/* The cheap bounds a kind offers for culling, else the ones it picks with. */
static bool cull_bounds(wgr_handle_t drawable, vec3_t *lmin, vec3_t *lmax, wgr_mat4_t *model)
{
    const wgr_handle_kind_t kind = wgr_handle_get_kind(drawable);
    if ((int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT && wgr_cull_bounds_registry[kind] != NULL) {
        return wgr_cull_bounds_registry[kind](drawable, lmin, lmax, model);
    }
    return wgr_drawable_bounds(drawable, lmin, lmax, model);
}

void wgr_scene_register_casts_shadow(wgr_handle_kind_t kind, wgr_drawable_casts_shadow_fn casts)
{
    if ((int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT) {
        wgr_casts_shadow_registry[kind] = casts;
    }
}

/* Whether this drawable throws a shadow (its kind may not do shadows at all). */
static bool drawable_casts_shadow(wgr_handle_t drawable)
{
    const wgr_handle_kind_t kind = wgr_handle_get_kind(drawable);
    return (int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT && wgr_casts_shadow_registry[kind] != NULL &&
           wgr_casts_shadow_registry[kind](drawable);
}

void wgr_scene_register_bounds(wgr_handle_kind_t kind, wgr_drawable_bounds_fn bounds)
{
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT) {
        return;
    }
    wgr_bounds_registry[kind] = bounds;
}

bool wgr_drawable_bounds(wgr_handle_t handle, vec3_t *lmin, vec3_t *lmax, wgr_mat4_t *model)
{
    wgr_handle_kind_t kind = wgr_handle_get_kind(handle);
    if ((int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT &&
        wgr_bounds_registry[kind] != NULL) {
        return wgr_bounds_registry[kind](handle, lmin, lmax, model);
    }
    return false;
}

void wgr_scene_register_pick(wgr_handle_kind_t kind, wgr_drawable_pick_fn pick)
{
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT) {
        return;
    }
    wgr_pick_registry[kind] = pick;
}

void wgr_scene_register_enabled(wgr_handle_kind_t kind, wgr_drawable_enabled_fn enabled)
{
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT) {
        return;
    }
    wgr_enabled_registry[kind] = enabled;
}

/* A 2D member: its kind draws and picks in screen space (sprite2d, text2d,
 * shape2d). Each kind is one or the other, so the handle says which. */
static bool is_2d_member(wgr_handle_t handle)
{
    const wgr_handle_kind_t kind = wgr_handle_get_kind(handle);
    return (int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT && wgr_passes_registry[kind].pick_2d != NULL;
}

/* The clip rectangle of a layer, or NULL when it isn't clipped. */
static const wgr_scene_clip_t *lookup_clip(const wgr_scene_t *scene_ptr, int layer)
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
static bool clipped_out(const wgr_scene_t *scene_ptr, int layer, float x, float y)
{
    const wgr_scene_clip_t *clip = lookup_clip(scene_ptr, layer);
    return clip != NULL &&
           (x < clip->x || y < clip->y || x > clip->x + clip->width || y > clip->y + clip->height);
}

static bool is_enabled(wgr_handle_t handle)
{
    const wgr_handle_kind_t kind = wgr_handle_get_kind(handle);
    if ((int)kind < 0 || (int)kind >= WGR_DRAWABLE_KIND_COUNT || wgr_enabled_registry[kind] == NULL) {
        return true;
    }
    return wgr_enabled_registry[kind](handle);
}

bool wgr_drawable_pick(wgr_handle_t handle, vec3_t origin, vec3_t dir, wgr_pick_result_t *out)
{
    wgr_handle_kind_t kind = wgr_handle_get_kind(handle);
    if ((int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT &&
        wgr_pick_registry[kind] != NULL) {
        return wgr_pick_registry[kind](handle, origin, dir, out);
    }
    return false;
}

/* ---- picking one drawable ---------------------------------------------- */

static wgr_pick_stats_t wgr_pick_stats;

/* A 2D drawable at a screen point (logical pixels). */
static bool pick_2d(wgr_handle_t drawable, const wgr_drawable_passes_t *passes, float x, float y,
                    wgr_pick_result_t *out)
{
    wgr_pick_stats.narrowphase_tests++;
    if (!passes->pick_2d(drawable, x, y, out)) {
        *out = (wgr_pick_result_t){0};
        return false;
    }
    wgr_pick_stats.narrowphase_hits++;
    out->hit = true;
    out->handle = drawable;
    return true;
}

/* A 3D drawable along a world ray: bounding box first, then the kind's exact test.
 * Kinds without an exact test count a bounding box hit. */
static bool pick_3d(wgr_handle_t drawable, wgr_ray_t ray, wgr_pick_result_t *out)
{
    const wgr_handle_kind_t kind = wgr_handle_get_kind(drawable);
    const bool has_exact = (int)kind >= 0 && (int)kind < WGR_DRAWABLE_KIND_COUNT && wgr_pick_registry[kind] != NULL;
    vec3_t lmin, lmax;
    wgr_mat4_t model;
    float broad_t;

    *out = (wgr_pick_result_t){0};
    if (!wgr_drawable_bounds(drawable, &lmin, &lmax, &model)) {
        return false; /* not a 3D drawable, or nothing loaded yet */
    }
    wgr_pick_stats.broadphase_tests++;
    if (!wgr_pick_ray_world_aabb(ray, lmin, lmax, model, &broad_t)) {
        wgr_pick_stats.broadphase_rejects++;
        return false;
    }
    if (has_exact) {
        wgr_pick_stats.narrowphase_tests++;
        /* false: hidden or not pickable; no hit: missed */
        if (!wgr_pick_registry[kind](drawable, ray.origin, ray.dir, out) || !out->hit) {
            *out = (wgr_pick_result_t){0};
            return false;
        }
        wgr_pick_stats.narrowphase_hits++;
    } else {
        const vec3_t wp = {ray.origin.x + ray.dir.x * broad_t, ray.origin.y + ray.dir.y * broad_t,
                           ray.origin.z + ray.dir.z * broad_t};
        out->hit = true;
        out->distance = broad_t;
        out->point_world = wp;
        out->point_local = wgr_mat4_mul_point(wgr_mat4_inverse(model), wp);
    }
    out->handle = drawable;
    return true;
}

/* ---- scene store ------------------------------------------------------- */

static wgr_scene_t *resolve(wgr_handle_t scene)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_scene_pool, scene, &index)) {
        if (scene != 0) {
            log_warn("Invalid scene handle (%u)", (unsigned int)scene);
        }
        return NULL;
    }
    return &wgr_scenes[index];
}

/* Membership: a hash index from handle to item, so adding, removing, destroying and
 * relayering are constant time however many members a scene has. Removing leaves a
 * hole (drawable 0); tidy() closes holes, restores layer order and rebuilds the index
 * before the members are walked (draw, pick, interaction). */
static unsigned slot_of(const wgr_scene_t *scene_ptr, wgr_handle_t drawable)
{
    return (unsigned)((drawable * 2654435761u) >> 7) & (unsigned)(scene_ptr->index_capacity - 1);
}

static int find_entry(wgr_scene_t *scene_ptr, wgr_handle_t drawable)
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

static void index_insert(wgr_scene_t *scene_ptr, int item)
{
    unsigned slot = slot_of(scene_ptr, scene_ptr->items[item].drawable);
    while (scene_ptr->index[slot] != 0) {
        slot = (slot + 1) & (unsigned)(scene_ptr->index_capacity - 1);
    }
    scene_ptr->index[slot] = item + 1;
}

/* Rebuild the index for the members as they are now, at a capacity for `wanted`. */
static bool rebuild_index(wgr_scene_t *scene_ptr, int wanted)
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
static void remove_entry(wgr_scene_t *scene_ptr, int item)
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
static void tidy(wgr_scene_t *scene_ptr)
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
        wgr_scene_entry_t key = scene_ptr->items[i];
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

WGR_KEEP
wgr_handle_t wgr_scene_create(void)
{
    wgr_handle_t handle = wgr_handle_pool_alloc(&wgr_scene_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("scene: pool full (%u)", (unsigned)wgr_scene_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_scene_pool, handle, &index);
    wgr_scenes[index] = (wgr_scene_t){.tonemap = WGR_TONEMAP_NEUTRAL, .culling = true};
    return handle;
}

WGR_KEEP
void wgr_scene_destroy(wgr_handle_t scene)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return;
    }
    free(scene_ptr->items);
    free(scene_ptr->index);
    release_environment(scene_ptr->environment); /* no-op for 0 */
    release_environment(scene_ptr->background);
    *scene_ptr = (wgr_scene_t){0};
    wgr_handle_pool_free(&wgr_scene_pool, scene);
}

WGR_KEEP
bool wgr_scene_add(wgr_handle_t scene, wgr_handle_t drawable, int layer)
{
    wgr_scene_t *scene_ptr = resolve(scene);
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
        wgr_scene_entry_t *grown = realloc(scene_ptr->items, (size_t)cap * sizeof(*grown));
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

WGR_KEEP
bool wgr_scene_set_layer(wgr_handle_t scene, wgr_handle_t drawable, int layer)
{
    wgr_scene_t *scene_ptr = resolve(scene);
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

WGR_KEEP
bool wgr_scene_remove(wgr_handle_t scene, wgr_handle_t drawable)
{
    wgr_scene_t *scene_ptr = resolve(scene);
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

void wgr_scene_forget(wgr_handle_t object)
{
    if (object == 0) {
        return;
    }
    for (uint16_t i = 1; i < wgr_scene_pool.capacity; i++) {
        wgr_scene_t *scene_ptr = &wgr_scenes[i];
        int idx;
        if (!wgr_scene_pool.occupied[i]) {
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

WGR_KEEP
void wgr_scene_clear(wgr_handle_t scene)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return;
    }
    scene_ptr->count = 0;
    scene_ptr->dirty = false;
    if (scene_ptr->index != NULL) {
        memset(scene_ptr->index, 0, sizeof(int) * (size_t)scene_ptr->index_capacity);
    }
}

WGR_KEEP
bool wgr_scene_set_clip(wgr_handle_t scene, int layer, float x, float y, float width, float height)
{
    wgr_scene_t *scene_ptr = resolve(scene);
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
            log_warn("wgr_scene_set_clip: at most %d clipped layers per scene", MAX_SCENE_CLIPS);
            return false;
        }
        slot = scene_ptr->clip_count++;
    }
    scene_ptr->clips[slot] = (wgr_scene_clip_t){.layer = layer, .x = x, .y = y, .width = width, .height = height};
    return true;
}

WGR_KEEP
void wgr_scene_set_active_camera(wgr_handle_t scene, wgr_handle_t camera)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return;
    }
    scene_ptr->camera = camera;
}

WGR_KEEP
bool wgr_scene_set_ambient(wgr_handle_t scene, wgr_color_t color, float intensity)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return false;
    }
    scene_ptr->ambient_color = color;
    scene_ptr->ambient_intensity = intensity > 0.0f ? intensity : 0.0f;
    return true;
}

WGR_KEEP
bool wgr_scene_set_environment(wgr_handle_t scene, wgr_handle_t environment, float intensity, float rotation)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL || (environment != 0 && wgr_handle_get_kind(environment) != WGR_HANDLE_KIND_ENVIRONMENT)) {
        return false;
    }
    retain_environment(environment); /* no-op for 0 */
    release_environment(scene_ptr->environment);
    scene_ptr->environment = environment;
    scene_ptr->environment_intensity = intensity > 0.0f ? intensity : 0.0f;
    scene_ptr->environment_rotation = rotation;
    return true;
}

WGR_KEEP
bool wgr_scene_set_background(wgr_handle_t scene, wgr_handle_t environment, float blur)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL || (environment != 0 && wgr_handle_get_kind(environment) != WGR_HANDLE_KIND_ENVIRONMENT)) {
        return false;
    }
    retain_environment(environment);
    release_environment(scene_ptr->background);
    scene_ptr->background = environment;
    scene_ptr->background_blur = blur < 0.0f ? 0.0f : (blur > 1.0f ? 1.0f : blur);
    return true;
}

WGR_KEEP
bool wgr_scene_set_tonemap(wgr_handle_t scene, wgr_tonemap_t tonemap, float exposure)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL || tonemap < WGR_TONEMAP_NONE || tonemap > WGR_TONEMAP_ACES) {
        return false;
    }
    scene_ptr->tonemap = tonemap;
    scene_ptr->exposure = exposure;
    return true;
}

/* Gather the scene's enabled lights (in member order) and ambient into a
 * lighting environment for the models drawn by this scene draw. */
static int push_lighting(const wgr_scene_t *scene_ptr)
{
    static wgr_light_env_t env; /* large; built and copied once per scene draw */
    wgr_colorf_t ambient = wgr_color_unpack(scene_ptr->ambient_color);

    env.count = 0;
    env.environment = scene_ptr->environment;
    env.environment_intensity = scene_ptr->environment_intensity;
    env.environment_rotation = scene_ptr->environment_rotation;
    env.tonemap = (int)scene_ptr->tonemap;
    env.exposure = scene_ptr->exposure;
    env.ambient = (vec3_t){wgr_srgb_to_linear(ambient.r) * scene_ptr->ambient_intensity,
                           wgr_srgb_to_linear(ambient.g) * scene_ptr->ambient_intensity,
                           wgr_srgb_to_linear(ambient.b) * scene_ptr->ambient_intensity};
    env.shadow_count = 0;
    for (int i = 0; i < scene_ptr->count && env.count < WGR_MAX_SCENE_LIGHTS; i++) {
        if (wgr_handle_get_kind(scene_ptr->items[i].drawable) == WGR_HANDLE_KIND_LIGHT &&
            wgr_scene_hooks.scene_light(scene_ptr->items[i].drawable, &env.lights[env.count])) {
            /* the first few casting lights get a shadow map; the rest light as usual */
            if (env.lights[env.count].casts_shadows && env.shadow_count < WGR_MAX_SHADOW_LIGHTS) {
                env.shadow_lights[env.shadow_count++] = env.count;
            }
            env.count++;
        }
    }
    return wgr_scene_hooks.light_env_push(&env);
}

/* One layer: opaque parts first, then transparent parts sorted back to front
 * across all drawable kinds. Consecutive parts of the same kind stay batched
 * (the render command list merges adjacent sokol_gl and model runs). */

/* Double the transparent list, up to WGR_MAX_TRANSPARENT_ITEMS; false when it can't. */
static bool grow_transparent_items(void)
{
    const int capacity = wgr_transparent_capacity == 0 ? TRANSPARENT_INITIAL : wgr_transparent_capacity * 2;
    wgr_transparent_item_t *items;
    if (wgr_transparent_capacity >= WGR_MAX_TRANSPARENT_ITEMS) {
        return false;
    }
    const int size = capacity < WGR_MAX_TRANSPARENT_ITEMS ? capacity : WGR_MAX_TRANSPARENT_ITEMS;
    items = realloc(wgr_transparent_items, sizeof(*items) * (size_t)size);
    if (items == NULL) {
        return false;
    }
    wgr_transparent_items = items;
    wgr_transparent_capacity = size;
    log_debug("scene: transparent list grown to %d parts", wgr_transparent_capacity);
    return true;
}

/* What this scene draw can see: the camera's planes, and for each casting light the
 * direction and reach of the shadows it throws, so a caster off screen that shadows
 * something on screen is still drawn. Rebuilt per scene draw. */
static struct {
    bool on;                 /* the scene's switch, and a camera to build planes from */
    wgr_plane_t planes[6];
    int shadow_count;
    vec3_t shadow_dir[WGR_MAX_SHADOW_LIGHTS];
    float shadow_reach[WGR_MAX_SHADOW_LIGHTS];
} wgr_cull;

static void begin_culling(const wgr_scene_t *scene_ptr, const wgr_camera3d_t *cam, int light_env)
{
    const wgr_light_env_t *env = NULL;
    vec2_t size = wgr_render_target_size();
    const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;

    wgr_cull.on = scene_ptr->culling;
    wgr_cull.shadow_count = 0;
    if (!wgr_cull.on) {
        return;
    }
    wgr_frustum_from_view_proj(wgr_mat4_mul(wgr_camera3d_projection(cam, aspect), wgr_camera3d_view(cam)),
                              wgr_cull.planes);
    if (wgr_scene_hooks.light_env_get != NULL && light_env >= 0) {
        env = wgr_scene_hooks.light_env_get(light_env);
    }
    for (int i = 0; env != NULL && i < env->shadow_count; i++) {
        const wgr_scene_light_t *light = &env->lights[env->shadow_lights[i]];
        const float reach = light->type == WGR_LIGHT_SPOT && light->range > 0.0f && light->range < light->shadow_distance
                                ? light->range
                                : light->shadow_distance;
        wgr_cull.shadow_dir[wgr_cull.shadow_count] = light->direction;
        wgr_cull.shadow_reach[wgr_cull.shadow_count++] = reach;
    }
}

/* Whether this member is worth submitting: inside the view, or a caster whose shadow
 * could fall inside it. Skinned bounds are the rest pose, so they are padded — better
 * to draw a little too much than to cull a raised arm. */
static bool visible(wgr_handle_t drawable)
{
    vec3_t lmin, lmax, wmin, wmax;
    wgr_mat4_t model;

    if (!wgr_cull.on || !cull_bounds(drawable, &lmin, &lmax, &model)) {
        return true; /* culling off, or nothing to test it with */
    }
    wgr_pick_world_aabb(lmin, lmax, model, &wmin, &wmax);
    wgr_aabb_pad(&wmin, &wmax, WGR_CULL_PAD);
    if (wgr_frustum_test_aabb(wgr_cull.planes, wmin, wmax)) {
        return true;
    }
    if (wgr_cull.shadow_count == 0 || !drawable_casts_shadow(drawable)) {
        return false;
    }
    for (int i = 0; i < wgr_cull.shadow_count; i++) { /* could its shadow reach the view? */
        vec3_t smin, smax;
        wgr_aabb_sweep(wmin, wmax, wgr_cull.shadow_dir[i], wgr_cull.shadow_reach[i], &smin, &smax);
        if (wgr_frustum_test_aabb(wgr_cull.planes, smin, smax)) {
            return true;
        }
    }
    return false;
}

static void draw_layer(const wgr_scene_entry_t *entries, int count, const wgr_camera3d_t *cam)
{
    int transparent_count = 0;

    /* opaque (and masked) parts: order doesn't matter, so sprites group by texture */
    begin_unordered();
    for (int i = 0; i < count; i++) {
        const wgr_drawable_passes_t *passes = lookup_passes(entries[i].drawable);
        if (passes != NULL && passes->draw_opaque != NULL && visible(entries[i].drawable)) {
            passes->draw_opaque(entries[i].drawable);
        }
    }
    end_unordered();

    for (int i = 0; i < count; i++) {
        const wgr_drawable_passes_t *passes = lookup_passes(entries[i].drawable);
        int first = transparent_count;
        int room, collected;
        if (passes == NULL || passes->collect_transparent == NULL || !visible(entries[i].drawable)) {
            continue;
        }
        /* a drawable that fills the room left may have had more: grow and collect it again */
        for (;;) {
            room = wgr_transparent_capacity - transparent_count;
            collected = room > 0 ? passes->collect_transparent(entries[i].drawable, cam,
                                                               &wgr_transparent_items[first], room)
                                 : 0;
            if (collected < room || !grow_transparent_items()) {
                break;
            }
        }
        if (room <= 0 || collected >= room) {
            if (!wgr_transparent_overflow_logged) {
                log_warn("scene: %d transparent parts in one layer, the most there can be; skipping the rest",
                         WGR_MAX_TRANSPARENT_ITEMS);
                wgr_transparent_overflow_logged = true;
            }
        }
        transparent_count += collected;
        for (int t = first; t < transparent_count; t++) {
            wgr_transparent_items[t].order = t;
        }
    }

    if (transparent_count > 0) {
        wgr_scene_sort_transparent(wgr_transparent_items, transparent_count);
        wgr_render_set_3d_transparent(true);
        for (int t = 0; t < transparent_count; t++) {
            const wgr_transparent_item_t *item = &wgr_transparent_items[t];
            const wgr_drawable_passes_t *passes = lookup_passes(item->handle);
            if (passes != NULL && passes->draw_transparent != NULL) {
                passes->draw_transparent(item->handle, item->part);
            }
        }
        wgr_render_set_3d_transparent(false);
    }

    /* additive parts, after the blended ones, unsorted */
    begin_unordered();
    for (int i = 0; i < count; i++) {
        const wgr_drawable_passes_t *passes = lookup_passes(entries[i].drawable);
        if (passes != NULL && passes->draw_additive != NULL) {
            passes->draw_additive(entries[i].drawable);
        }
    }
    end_unordered();
}

WGR_KEEP
void wgr_scene_draw(wgr_handle_t scene)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    wgr_camera3d_t cam;
    if (scene_ptr == NULL) {
        return;
    }

    if (scene_ptr->camera != 0) {
        wgr_camera3d_set_active(scene_ptr->camera);
    }

    tidy(scene_ptr);

    if (!wgr_camera3d_get_active_data(&cam)) {
        return;
    }

    int light_env = -1;
    if (wgr_scene_hooks.light_env_push != NULL) { /* lights linked: what the models drawn here see */
        light_env = push_lighting(scene_ptr);
        wgr_scene_hooks.light_env_set_current(light_env);
    }
    if (scene_ptr->background != 0 && wgr_scene_hooks.environment_background != NULL) {
        const bool same = scene_ptr->background == scene_ptr->environment;
        wgr_scene_hooks.environment_background(scene_ptr->background, scene_ptr->background_blur,
                                         same ? scene_ptr->environment_intensity : 1.0f,
                                         same ? scene_ptr->environment_rotation : 0.0f,
                                         (int)scene_ptr->tonemap, scene_ptr->exposure);
    }
    begin_culling(scene_ptr, &cam, light_env);
    wgr_render_begin_mode_3d();
    for (int start = 0; start < scene_ptr->count;) {
        int layer = scene_ptr->items[start].layer;
        int end = start;
        while (end < scene_ptr->count && scene_ptr->items[end].layer == layer) {
            end++;
        }
        draw_layer(&scene_ptr->items[start], end - start, &cam);
        start = end;
    }
    wgr_render_end_mode_3d();
    if (wgr_scene_hooks.light_env_set_current != NULL) {
        wgr_scene_hooks.light_env_set_current(-1); /* models drawn outside a scene are unlit */
    }

    /* 2D members on top of all 3D, in layer then member order, each layer inside
     * its clip rectangle (wgr_scene_set_clip) if it has one */
    {
        const wgr_scene_clip_t *clip = NULL;
        int clip_layer = 0;
        bool layer_known = false;
        for (int i = 0; i < scene_ptr->count; i++) {
            const wgr_handle_t drawable = scene_ptr->items[i].drawable;
            const wgr_drawable_passes_t *passes = lookup_passes(drawable);
            if (passes == NULL || passes->draw_2d == NULL || !is_2d_member(drawable)) {
                continue;
            }
            if (!layer_known || scene_ptr->items[i].layer != clip_layer) {
                const wgr_scene_clip_t *next = lookup_clip(scene_ptr, scene_ptr->items[i].layer);
                if (clip != NULL) {
                    wgr_render_pop_clip(); /* leaving the previous layer's clip */
                }
                if (next != NULL) {
                    wgr_render_push_clip(next->x, next->y, next->width, next->height);
                }
                clip = next;
                clip_layer = scene_ptr->items[i].layer;
                layer_known = true;
            }
            passes->draw_2d(drawable);
        }
        if (clip != NULL) {
            wgr_render_pop_clip();
        }
    }
}

/* ---- picking ----------------------------------------------------------- */

WGR_KEEP
wgr_pick_result_t wgr_scene_pick(wgr_handle_t scene, wgr_handle_t camera,
                               float mouse_x, float mouse_y)
{
    wgr_pick_result_t result = {0};
    wgr_scene_t *scene_ptr = resolve(scene);
    wgr_camera3d_t cam;
    wgr_ray_t ray;
    vec2_t screen;
    float best_t = 1e30f;

    if (scene_ptr == NULL) {
        return result;
    }

    /* 2D members are drawn on top of 3D, so they're hit first: topmost (last
     * drawn) first */
    tidy(scene_ptr);
    for (int i = scene_ptr->count - 1; i >= 0; i--) {
        const wgr_drawable_passes_t *passes = lookup_passes(scene_ptr->items[i].drawable);
        if (is_2d_member(scene_ptr->items[i].drawable) &&
            !clipped_out(scene_ptr, scene_ptr->items[i].layer, mouse_x, mouse_y) &&
            pick_2d(scene_ptr->items[i].drawable, passes, mouse_x, mouse_y, &result)) {
            return result;
        }
    }
    result = (wgr_pick_result_t){0};

    if (camera == 0) {
        camera = scene_ptr->camera;
    }
    if (camera != 0) {
        wgr_camera3d_set_active(camera);
    }
    if (!wgr_camera3d_get_active_data(&cam)) {
        return result;
    }

    screen = wgr_window_get_screen_size();
    ray = wgr_pick_ray_from_screen(&cam, mouse_x, mouse_y, screen.x, screen.y);

    for (int i = 0; i < scene_ptr->count; i++) {
        wgr_pick_result_t hit = {0};
        if (!is_2d_member(scene_ptr->items[i].drawable) && pick_3d(scene_ptr->items[i].drawable, ray, &hit) &&
            hit.distance < best_t) {
            best_t = hit.distance;
            result = hit;
        }
    }
    return result;
}

WGR_KEEP
wgr_pick_result_t wgr_pick_object(wgr_handle_t object, wgr_handle_t camera, float x, float y)
{
    wgr_pick_result_t result = {0};
    const wgr_drawable_passes_t *passes = lookup_passes(object);
    wgr_camera3d_t cam;
    vec2_t screen;

    if (object == 0) {
        return result;
    }
    if (is_2d_member(object)) {
        pick_2d(object, passes, x, y, &result);
        return result;
    }
    if (camera != 0) {
        wgr_camera3d_set_active(camera);
    }
    if (!wgr_camera3d_get_active_data(&cam)) {
        return result;
    }
    screen = wgr_window_get_screen_size();
    pick_3d(object, wgr_pick_ray_from_screen(&cam, x, y, screen.x, screen.y), &result);
    return result;
}

WGR_KEEP
wgr_pick_stats_t wgr_pick_get_stats(void)
{
    return wgr_pick_stats;
}

WGR_KEEP
void wgr_pick_reset_stats(void)
{
    wgr_pick_stats = (wgr_pick_stats_t){0};
}

/* ---- pointer interaction ---------------------------------------------- */

/* The topmost pickable member under a screen point: 2D members first (last drawn
 * first), then the nearest 3D hit through the scene's camera. Unlike wgr_scene_pick it
 * doesn't change the active camera or count pick statistics. */
static wgr_handle_t pick_member(wgr_scene_t *scene_ptr, float x, float y, bool *is_2d)
{
    const wgr_pick_stats_t stats = wgr_pick_stats;
    wgr_pick_result_t result = {0};
    wgr_handle_t hit = 0;
    float best_t = 1e30f;
    wgr_camera3d_t cam;

    *is_2d = false;
    tidy(scene_ptr);
    for (int i = scene_ptr->count - 1; i >= 0 && hit == 0; i--) {
        const wgr_drawable_passes_t *passes = lookup_passes(scene_ptr->items[i].drawable);
        if (is_2d_member(scene_ptr->items[i].drawable) &&
            !clipped_out(scene_ptr, scene_ptr->items[i].layer, x, y) &&
            pick_2d(scene_ptr->items[i].drawable, passes, x, y, &result)) {
            hit = scene_ptr->items[i].drawable;
            *is_2d = true;
        }
    }
    if (hit == 0 && wgr_camera3d_get_data(scene_ptr->camera, &cam)) {
        const vec2_t screen = wgr_window_get_screen_size();
        const wgr_ray_t ray = wgr_pick_ray_from_screen(&cam, x, y, screen.x, screen.y);
        for (int i = 0; i < scene_ptr->count; i++) {
            wgr_pick_result_t candidate = {0};
            if (!is_2d_member(scene_ptr->items[i].drawable) && pick_3d(scene_ptr->items[i].drawable, ray, &candidate) &&
                candidate.distance < best_t) {
                best_t = candidate.distance;
                hit = scene_ptr->items[i].drawable;
            }
        }
    }
    wgr_pick_stats = stats;
    return hit;
}

static void add_edge(wgr_handle_t *list, int *count, wgr_handle_t handle)
{
    if (*count < WGR_INTERACTION_MAX_EDGES) {
        list[(*count)++] = handle;
    }
}

static void add_interaction_edges(wgr_interaction_t *state, wgr_handle_t entered, wgr_handle_t left, wgr_handle_t pressed,
                                  wgr_handle_t released, wgr_handle_t clicked)
{
    wgr_interaction_edges_t *sets[2] = {&state->frame_edges, &state->tick_edges};
    for (int i = 0; i < 2; i++) {
        if (entered != 0) add_edge(sets[i]->entered, &sets[i]->entered_count, entered);
        if (left != 0) add_edge(sets[i]->left, &sets[i]->left_count, left);
        if (pressed != 0) sets[i]->pressed = pressed;
        if (released != 0) sets[i]->released = released;
        if (clicked != 0) sets[i]->clicked = clicked;
    }
}

void wgr_scene_update_interaction(void)
{
    float x, y;
    bool down, pressed, released;
    bool captured = false;

    wgr_input_get_pointer_frame(&x, &y, &down, &pressed, &released);
    if (wgr_scene_capture_releasing) {
        wgr_input_set_scene_pointer_captured(false); /* captured through the release frame */
        wgr_scene_capture_releasing = false;
    }
    for (int i = 0; i < wgr_scene_pool.capacity; i++) {
        wgr_scene_t *scene_ptr = &wgr_scenes[i];
        wgr_interaction_t *state = &scene_ptr->interaction;
        wgr_handle_t entered = 0, left = 0, pressed_on = 0, released_on = 0, clicked_on = 0;
        bool is_2d = false;
        wgr_handle_t hit;

        if (!wgr_scene_pool.occupied[i] || !state->interactive) {
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
        wgr_input_set_scene_pointer_captured(true);
    }
    if (released) {
        wgr_scene_capture_releasing = true; /* clears next frame */
    }
}

void wgr_scene_end_tick_interaction(void)
{
    for (int i = 0; i < wgr_scene_pool.capacity; i++) {
        wgr_scenes[i].interaction.tick_edges = (wgr_interaction_edges_t){0};
    }
}

void wgr_scene_end_frame_interaction(void)
{
    for (int i = 0; i < wgr_scene_pool.capacity; i++) {
        wgr_scenes[i].interaction.frame_edges = (wgr_interaction_edges_t){0};
    }
}

static const wgr_interaction_edges_t *current_interaction_edges(const wgr_scene_t *scene_ptr)
{
    return wgr_input_get_context() == WGR_INPUT_CONTEXT_TICK ? &scene_ptr->interaction.tick_edges
                                                            : &scene_ptr->interaction.frame_edges;
}

static bool contains(const wgr_handle_t *list, int count, wgr_handle_t handle)
{
    for (int i = 0; i < count; i++) {
        if (list[i] == handle) return true;
    }
    return false;
}

WGR_KEEP
WGR_KEEP
bool wgr_scene_set_culling(wgr_handle_t scene, bool culling)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) return false;
    scene_ptr->culling = culling;
    return true;
}

WGR_KEEP
bool wgr_scene_is_culling(wgr_handle_t scene)
{
    const wgr_scene_t *scene_ptr = resolve(scene);
    return scene_ptr != NULL && scene_ptr->culling;
}

bool wgr_scene_set_interactive(wgr_handle_t scene, bool interactive)
{
    wgr_scene_t *scene_ptr = resolve(scene);
    if (scene_ptr == NULL) {
        return false;
    }
    scene_ptr->interaction = (wgr_interaction_t){.interactive = interactive};
    return true;
}

WGR_KEEP
bool wgr_scene_is_interactive(wgr_handle_t scene)
{
    const wgr_scene_t *scene_ptr = resolve(scene);
    return scene_ptr != NULL && scene_ptr->interaction.interactive;
}

WGR_KEEP
wgr_handle_t wgr_scene_get_hovered(wgr_handle_t scene)
{
    const wgr_scene_t *scene_ptr = resolve(scene);
    return scene_ptr != NULL ? scene_ptr->interaction.hovered : 0;
}

WGR_KEEP
wgr_button_state_t wgr_scene_get_hover(wgr_handle_t scene, wgr_handle_t object)
{
    const wgr_scene_t *scene_ptr = resolve(scene);
    const wgr_interaction_edges_t *edges;
    if (scene_ptr == NULL || object == 0 || !is_enabled(object)) {
        return WGR_BUTTON_UP;
    }
    edges = current_interaction_edges(scene_ptr);
    if (contains(edges->entered, edges->entered_count, object)) return WGR_BUTTON_PRESSED;
    if (contains(edges->left, edges->left_count, object)) return WGR_BUTTON_RELEASED;
    return scene_ptr->interaction.hovered == object ? WGR_BUTTON_DOWN : WGR_BUTTON_UP;
}

WGR_KEEP
wgr_button_state_t wgr_scene_get_press(wgr_handle_t scene, wgr_handle_t object)
{
    const wgr_scene_t *scene_ptr = resolve(scene);
    const wgr_interaction_edges_t *edges;
    if (scene_ptr == NULL || object == 0 || !is_enabled(object)) {
        return WGR_BUTTON_UP;
    }
    edges = current_interaction_edges(scene_ptr);
    if (edges->pressed == object) return WGR_BUTTON_PRESSED;
    if (edges->released == object) return WGR_BUTTON_RELEASED;
    return scene_ptr->interaction.press_down && scene_ptr->interaction.press_target == object ? WGR_BUTTON_DOWN
                                                                                               : WGR_BUTTON_UP;
}

WGR_KEEP
bool wgr_scene_is_clicked(wgr_handle_t scene, wgr_handle_t object)
{
    const wgr_scene_t *scene_ptr = resolve(scene);
    return scene_ptr != NULL && object != 0 && is_enabled(object) &&
           current_interaction_edges(scene_ptr)->clicked == object;
}

void wgr_scene_init(void)
{
    memset(wgr_passes_registry, 0, sizeof(wgr_passes_registry));
    memset(wgr_bounds_registry, 0, sizeof(wgr_bounds_registry));
    memset(wgr_pick_registry, 0, sizeof(wgr_pick_registry));
    memset(wgr_enabled_registry, 0, sizeof(wgr_enabled_registry));
    wgr_scene_capture_releasing = false;
    if (!wgr_handle_pool_init(&wgr_scene_pool, WGR_HANDLE_KIND_SCENE, "scene", (void **)&wgr_scenes,
                             sizeof(wgr_scene_t), SCENES_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("scene: out of memory");
    }
}

void wgr_scene_deinit(void)
{
    free(wgr_sort_scratch);
    wgr_sort_scratch = NULL;
    wgr_sort_scratch_capacity = 0;
    free(wgr_transparent_items);
    wgr_transparent_items = NULL;
    wgr_transparent_capacity = 0;
    for (int i = 0; i < wgr_scene_pool.capacity; i++) {
        free(wgr_scenes[i].items);
        free(wgr_scenes[i].index);
        wgr_scenes[i] = (wgr_scene_t){0};
    }
    wgr_handle_pool_destroy(&wgr_scene_pool);
}
