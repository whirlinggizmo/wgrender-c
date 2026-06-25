#include "rl_text3d.h"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_color.h"
#include "internal/rl_font.h"
#include "internal/rl_handle_pool.h"
#include "internal/rl_render_pass.h"
#include "internal/rl_scene.h"
#include "internal/rl_text3d.h"
#include "rl_scratch.h"
#include "rl_sprite3d.h"

#define MAX_TEXT3D 256
#define MAX_TEXT3D_CONTENT 512

typedef struct
{
    bool in_use;
    rl_handle_t font;
    rl_handle_t color;
    float size;
    float position_x;
    float position_y;
    float position_z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
    char content[MAX_TEXT3D_CONTENT];
    bool visible;
    bool pickable;
    rl_sprite3d_facing_t facing;
    Vector2 cached_bounds;
    bool bounds_dirty;
} rl_text3d_instance_t;

static rl_text3d_instance_t rl_text3d[MAX_TEXT3D];
static rl_handle_pool_t rl_text3d_pool;
static uint16_t rl_text3d_free_indices[MAX_TEXT3D];
static uint16_t rl_text3d_generations[MAX_TEXT3D];
static unsigned char rl_text3d_occupied[MAX_TEXT3D];

static rl_text3d_instance_t *resolve_text3d_instance(rl_handle_t handle)
{
    uint16_t index = 0;
    if (!rl_handle_pool_resolve(&rl_text3d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid text3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_text3d[index].in_use) {
        log_warn("Stale text3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_text3d[index];
}

/* Draw text as 3D quads in the current local coordinate frame (after billboard matrix push).
 * Glyphs start at local (0,0,0) and extend in +X (width) and +Y (height).
 * This mirrors the raylib DrawText3D / DrawTextCodepoint3D logic from raylib-extras. */
static void text3d_draw_glyphs(Font font, const char *text, float size, Color tint)
{
    float scale = size / (float)font.baseSize;
    float tex_w  = (float)font.texture.width;
    float tex_h  = (float)font.texture.height;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    int i = 0;

    rlSetTexture(font.texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);

    while (text[i] != '\0') {
        int cp_size = 0;
        int cp = GetCodepointNext(&text[i], &cp_size);

        if (cp == '\n') {
            offset_y -= (scale + scale);
            offset_x  = 0.0f;
        } else if (cp != ' ' && cp != '\t') {
            int gi = GetGlyphIndex(font, cp);
            Rectangle rec   = font.recs[gi];
            GlyphInfo glyph = font.glyphs[gi];

            float gx = offset_x + (float)glyph.offsetX * scale;
            float gy = offset_y - (float)glyph.offsetY * scale;
            float gw = rec.width  * scale;
            float gh = rec.height * scale;

            float u0 = rec.x / tex_w;
            float v0 = rec.y / tex_h;
            float u1 = (rec.x + rec.width)  / tex_w;
            float v1 = (rec.y + rec.height) / tex_h;

            /* top-left, bottom-left, bottom-right, top-right (CCW from +Z) */
            rlTexCoord2f(u0, v0); rlVertex3f(gx,      gy + gh, 0.0f);
            rlTexCoord2f(u0, v1); rlVertex3f(gx,      gy,      0.0f);
            rlTexCoord2f(u1, v1); rlVertex3f(gx + gw, gy,      0.0f);
            rlTexCoord2f(u1, v0); rlVertex3f(gx + gw, gy + gh, 0.0f);
        }

        if (cp != '\n') {
            int gi = GetGlyphIndex(font, cp);
            if (font.glyphs[gi].advanceX == 0)
                offset_x += (font.recs[gi].width + 1.0f) * scale;
            else
                offset_x += (float)(font.glyphs[gi].advanceX + 1) * scale;
        }

        i += cp_size;
    }

    rlEnd();
    rlSetTexture(0);
}

static Vector2 text3d_measure_bounds(const rl_text3d_instance_t *text)
{
    if (text->content[0] == '\0' || text->font == 0 || text->size <= 0.0f) {
        return (Vector2){0.0f, 0.0f};
    }
    Font f = rl_font_get(text->font);
    float scale = text->size / (float)f.baseSize;
    Vector2 px = MeasureTextEx(f, text->content, (float)f.baseSize, 1.0f);
    return (Vector2){ px.x * scale, px.y * scale };
}

static Vector2 text3d_get_bounds(rl_text3d_instance_t *text)
{
    if (text->bounds_dirty) {
        text->cached_bounds = text3d_measure_bounds(text);
        text->bounds_dirty = false;
    }
    return text->cached_bounds;
}

/* Compute the right and up world-space vectors for this instance's facing mode. */
static bool text3d_get_axes(const rl_text3d_instance_t *text,
                             Camera3D camera,
                             Vector3 *out_right,
                             Vector3 *out_up)
{
    Vector3 right = {0};
    Vector3 up = {0};

    switch (text->facing) {
    case RL_SPRITE3D_FACING_CAMERA: {
        Matrix mv = MatrixLookAt(camera.position, camera.target, camera.up);
        right = Vector3Normalize((Vector3){mv.m0, mv.m4, mv.m8});
        up    = Vector3Normalize(camera.up);
        break;
    }
    case RL_SPRITE3D_FACING_CAMERA_FIXED_Y: {
        Matrix mv = MatrixLookAt(camera.position, camera.target, (Vector3){0.0f, 1.0f, 0.0f});
        right = Vector3Normalize((Vector3){mv.m0, mv.m4, mv.m8});
        up    = (Vector3){0.0f, 1.0f, 0.0f};
        break;
    }
    case RL_SPRITE3D_FACING_Y_UP:
        right = (Vector3){1.0f, 0.0f,  0.0f};
        up    = (Vector3){0.0f, 0.0f, -1.0f};
        break;
    case RL_SPRITE3D_FACING_FREE: {
        Matrix rot = MatrixRotateXYZ((Vector3){text->rotation_x, text->rotation_y, text->rotation_z});
        right = Vector3Normalize((Vector3){rot.m0, rot.m1, rot.m2});
        up    = Vector3Normalize((Vector3){rot.m4, rot.m5, rot.m6});
        break;
    }
    default:
        return false;
    }

    if (Vector3Length(right) <= 0.000001f || Vector3Length(up) <= 0.000001f) {
        return false;
    }

    if (out_right) *out_right = right;
    if (out_up)    *out_up    = up;
    return true;
}

/* Build 4 world-space corner points: bottom-left, bottom-right, top-right, top-left. */
static bool text3d_build_quad(const rl_text3d_instance_t *text,
                               Camera3D camera,
                               Vector2 bounds,
                               Vector3 points[4],
                               Vector3 *out_right,
                               Vector3 *out_up)
{
    Vector3 position = {text->position_x, text->position_y, text->position_z};
    Vector3 right = {0};
    Vector3 up = {0};
    float half_w = bounds.x * 0.5f;
    float half_h = bounds.y * 0.5f;

    if (!text3d_get_axes(text, camera, &right, &up)) {
        return false;
    }

    points[0] = Vector3Add(position, Vector3Add(Vector3Scale(right, -half_w), Vector3Scale(up, -half_h)));
    points[1] = Vector3Add(position, Vector3Add(Vector3Scale(right,  half_w), Vector3Scale(up, -half_h)));
    points[2] = Vector3Add(position, Vector3Add(Vector3Scale(right,  half_w), Vector3Scale(up,  half_h)));
    points[3] = Vector3Add(position, Vector3Add(Vector3Scale(right, -half_w), Vector3Scale(up,  half_h)));

    if (out_right) *out_right = right;
    if (out_up)    *out_up    = up;
    return true;
}

/* Push an rlgl matrix that orients the local XY plane as a billboard centered on position. */
static bool text3d_push_billboard_matrix(const rl_text3d_instance_t *text,
                                          Camera3D camera,
                                          Vector2 bounds)
{
    Vector3 position = {text->position_x, text->position_y, text->position_z};
    Vector3 right = {0};
    Vector3 up = {0};
    float half_w = bounds.x * 0.5f;
    float half_h = bounds.y * 0.5f;

    if (!text3d_get_axes(text, camera, &right, &up)) {
        return false;
    }

    Vector3 normal = Vector3CrossProduct(right, up);
    float tx = position.x - right.x * half_w - up.x * half_h;
    float ty = position.y - right.y * half_w - up.y * half_h;
    float tz = position.z - right.z * half_w - up.z * half_h;

    /* Column-major matrix: col0=right, col1=up, col2=normal, col3=translation */
    Matrix m;
    m.m0  = right.x;  m.m1  = right.y;  m.m2  = right.z;  m.m3  = 0.0f;
    m.m4  = up.x;     m.m5  = up.y;     m.m6  = up.z;     m.m7  = 0.0f;
    m.m8  = normal.x; m.m9  = normal.y; m.m10 = normal.z; m.m11 = 0.0f;
    m.m12 = tx;       m.m13 = ty;       m.m14 = tz;       m.m15 = 1.0f;

    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(m));
    return true;
}

RL_KEEP
rl_handle_t rl_text3d_create(rl_handle_t font, float size)
{
    rl_handle_t handle = 0;
    uint16_t index = 0;

    handle = rl_handle_pool_alloc(&rl_text3d_pool);
    if (handle == 0) {
        log_error("MAX_TEXT3D reached (%u)", MAX_TEXT3D);
        return 0;
    }
    rl_handle_pool_resolve(&rl_text3d_pool, handle, &index);

    rl_text3d[index].font = font;
    rl_text3d[index].color = 0;
    rl_text3d[index].size = size;
    rl_text3d[index].position_x = 0.0f;
    rl_text3d[index].position_y = 0.0f;
    rl_text3d[index].position_z = 0.0f;
    rl_text3d[index].rotation_x = 0.0f;
    rl_text3d[index].rotation_y = 0.0f;
    rl_text3d[index].rotation_z = 0.0f;
    rl_text3d[index].content[0] = '\0';
    rl_text3d[index].visible = true;
    rl_text3d[index].pickable = false;
    rl_text3d[index].facing = RL_SPRITE3D_FACING_CAMERA;
    rl_text3d[index].cached_bounds = (Vector2){0.0f, 0.0f};
    rl_text3d[index].bounds_dirty = true;
    rl_text3d[index].in_use = true;

    return handle;
}

RL_KEEP
void rl_text3d_set_font(rl_handle_t handle, rl_handle_t font)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return;
    text->font = font;
    text->bounds_dirty = true;
}

RL_KEEP
void rl_text3d_set_size(rl_handle_t handle, float size)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return;
    text->size = size;
    text->bounds_dirty = true;
}

RL_KEEP
void rl_text3d_set_content(rl_handle_t handle, const char *content)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return;
    strncpy(text->content, content, MAX_TEXT3D_CONTENT - 1);
    text->content[MAX_TEXT3D_CONTENT - 1] = '\0';
    text->bounds_dirty = true;
}

RL_KEEP
bool rl_text3d_set_transform(rl_handle_t handle,
                              float x, float y, float z,
                              float rx, float ry, float rz)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    text->position_x = x;
    text->position_y = y;
    text->position_z = z;
    text->rotation_x = rx;
    text->rotation_y = ry;
    text->rotation_z = rz;
    return true;
}

RL_KEEP
void rl_text3d_set_color(rl_handle_t handle, rl_handle_t color)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return;
    text->color = color;
}

RL_KEEP
bool rl_text3d_set_facing(rl_handle_t handle, int facing)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    text->facing = (rl_sprite3d_facing_t)facing;
    return true;
}

RL_KEEP
bool rl_text3d_set_visible(rl_handle_t handle, bool visible)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    text->visible = visible;
    return true;
}

RL_KEEP
bool rl_text3d_set_pickable(rl_handle_t handle, bool pickable)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    text->pickable = pickable;
    return true;
}

RL_KEEP
bool rl_text3d_is_visible(rl_handle_t handle)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    return text->visible;
}

RL_KEEP
bool rl_text3d_is_pickable(rl_handle_t handle)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    return text->pickable;
}

RL_KEEP
vec2_t rl_text3d_get_bounds(rl_handle_t handle)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return (vec2_t){0.0f, 0.0f};
    Vector2 b = text3d_get_bounds(text);
    return (vec2_t){b.x, b.y};
}

RL_KEEP
bool rl_text3d_get_bounds_to_scratch(rl_handle_t handle)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) {
        rl_scratch_set_vector2(0.0f, 0.0f);
        return false;
    }
    Vector2 b = text3d_get_bounds(text);
    rl_scratch_set_vector2(b.x, b.y);
    return true;
}

RL_KEEP
void rl_text3d_draw(rl_handle_t handle)
{
    rl_text3d_draw_pass(handle, RL_RENDER_PASS_TRANSPARENT_3D);
}

RL_KEEP
void rl_text3d_draw_text(const char *text, rl_handle_t font,
                          float x, float y, float z,
                          float size, rl_handle_t color)
{
    if (text == NULL || text[0] == '\0') return;
    Font f = rl_font_get(font);
    Color c = rl_color_get(color);
    /* Immediate mode: push a translate-only matrix for world position, then draw glyphs. */
    rlPushMatrix();
    rlTranslatef(x, y, z);
    text3d_draw_glyphs(f, text, size, c);
    rlPopMatrix();
}

RL_KEEP
void rl_text3d_destroy(rl_handle_t handle)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return;
    text->font = 0;
    text->color = 0;
    text->content[0] = '\0';
    text->in_use = false;
    rl_scene_on_drawable_destroy(handle);
    rl_handle_pool_free(&rl_text3d_pool, handle);
}

/* --- internal --- */

bool rl_text3d_is_valid(rl_handle_t handle)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    return text != NULL;
}

bool rl_text3d_has_render_pass(rl_handle_t handle, rl_render_pass_t pass)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    if (!text->visible) return false;
    return pass == RL_RENDER_PASS_TRANSPARENT_3D;
}

void rl_text3d_draw_pass(rl_handle_t handle, rl_render_pass_t pass)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    Camera3D camera = {0};

    if (pass != RL_RENDER_PASS_TRANSPARENT_3D) return;
    if (text == NULL) return;
    if (!text->visible) return;
    if (text->content[0] == '\0') return;
    if (text->font == 0) return;

    if (!rl_camera3d_get_active_camera(&camera)) {
        log_error("rl_text3d_draw requires an active 3D camera");
        return;
    }

    Vector2 bounds = text3d_get_bounds(text);
    if (bounds.x <= 0.0f && bounds.y <= 0.0f) return;

    Font f = rl_font_get(text->font);
    Color c = rl_color_get(text->color);

    if (!text3d_push_billboard_matrix(text, camera, bounds)) {
        return;
    }
    text3d_draw_glyphs(f, text->content, text->size, c);
    rlPopMatrix();
}

bool rl_text3d_get_position(rl_handle_t handle, float *x, float *y, float *z)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    if (x) *x = text->position_x;
    if (y) *y = text->position_y;
    if (z) *z = text->position_z;
    return true;
}

bool rl_text3d_is_pickable_internal(rl_handle_t handle)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    if (text == NULL) return false;
    return text->pickable;
}

bool rl_text3d_scene_pick_broadphase(rl_handle_t handle, Ray ray)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    Vector3 position = {0};
    float radius = 0.0f;
    RayCollision broad = {0};
    Vector2 bounds = {0};

    if (text == NULL) return false;
    if (!text->pickable) return false;
    if (text->content[0] == '\0') return false;

    bounds = text3d_get_bounds(text);
    if (bounds.x <= 0.0f && bounds.y <= 0.0f) return false;

    position = (Vector3){text->position_x, text->position_y, text->position_z};
    radius = sqrtf((bounds.x * 0.5f) * (bounds.x * 0.5f) +
                   (bounds.y * 0.5f) * (bounds.y * 0.5f));
    broad = GetRayCollisionSphere(ray, position, radius);
    return broad.hit;
}

RayCollision rl_text3d_get_ray_collision(rl_handle_t handle, Camera3D camera, Ray ray)
{
    rl_text3d_instance_t *text = resolve_text3d_instance(handle);
    RayCollision result = {0};
    Vector3 points[4] = {0};
    Vector2 bounds = {0};

    if (text == NULL) return result;
    if (!text->pickable) return result;
    if (text->content[0] == '\0') return result;

    bounds = text3d_get_bounds(text);
    if (bounds.x <= 0.0f && bounds.y <= 0.0f) return result;

    if (!text3d_build_quad(text, camera, bounds, points, NULL, NULL)) {
        return result;
    }

    result = GetRayCollisionQuad(ray, points[0], points[1], points[2], points[3]);
    return result;
}

void rl_text3d_init(void)
{
    rl_handle_pool_init(&rl_text3d_pool,
                        RL_HANDLE_KIND_TEXT3D,
                        MAX_TEXT3D,
                        rl_text3d_free_indices,
                        MAX_TEXT3D,
                        rl_text3d_generations,
                        rl_text3d_occupied);
    for (int i = 0; i < MAX_TEXT3D; i++) {
        rl_text3d[i].in_use = false;
        rl_text3d[i].font = 0;
        rl_text3d[i].color = 0;
        rl_text3d[i].size = 0.0f;
        rl_text3d[i].position_x = 0.0f;
        rl_text3d[i].position_y = 0.0f;
        rl_text3d[i].position_z = 0.0f;
        rl_text3d[i].rotation_x = 0.0f;
        rl_text3d[i].rotation_y = 0.0f;
        rl_text3d[i].rotation_z = 0.0f;
        rl_text3d[i].content[0] = '\0';
        rl_text3d[i].visible = true;
        rl_text3d[i].pickable = false;
        rl_text3d[i].facing = RL_SPRITE3D_FACING_CAMERA;
        rl_text3d[i].cached_bounds = (Vector2){0.0f, 0.0f};
        rl_text3d[i].bounds_dirty = true;
    }
}

void rl_text3d_deinit(void)
{
    int unloaded = 0;
    for (uint16_t i = 1; i < MAX_TEXT3D; i++) {
        if (!rl_text3d[i].in_use) continue;
        rl_handle_t handle = rl_handle_pool_handle_from_index(&rl_text3d_pool, i);
        if (handle == 0) continue;
        rl_text3d_destroy(handle);
        unloaded++;
    }
    rl_handle_pool_reset(&rl_text3d_pool);
    log_info("rl_text3d_deinit: Freed %d text3d instances", unloaded);
}
