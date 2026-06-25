#include "sk_model.h"

#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_math.h"
#include "internal/sk_model.h"
#include "internal/sk_pick.h"
#include "internal/sk_scene.h"
#include "sk_logger.h"

#include "cgltf.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "stb_image.h"

#define MAX_MODELS 256
#define MAX_DRAW_QUEUE 512
#define SK_MAX_JOINTS 128

/* ----------------------------------------------------------- data types ---- */

typedef struct {
    sg_buffer vbuf;
    sg_buffer ibuf;
    int index_count;
    sg_view view;
    sg_sampler sampler;
    float base_color[4];
    bool skinned;
    vec3_t pmin, pmax; /* local-space AABB (for picking) */
    /* CPU-side bind-pose geometry retained for narrow-phase triangle picking */
    float *pick_positions; /* 3 floats per vertex, mesh-local space */
    uint32_t *pick_indices;
    int pick_vertex_count;
} sk_primitive_t;

/* node transform (base from glTF + per-frame working copy) */
typedef struct {
    vec3_t t; quat_t r; vec3_t s;
    int parent;
} sk_node_t;

typedef struct {
    int node;        /* target node index */
    int path;        /* 0=T, 1=R, 2=S */
    int interp;      /* 0=linear, 1=step */
    float *times;
    float *values;   /* 3 per key (T/S) or 4 (R) */
    int key_count;
} sk_anim_channel_t;

typedef struct {
    sk_anim_channel_t *channels;
    int channel_count;
    float duration;
} sk_animation_t;

typedef struct {
    sk_primitive_t *prims;
    int prim_count;

    /* skeleton */
    sk_node_t *nodes;
    int node_count;
    int *joint_nodes;
    sk_mat4_t *inverse_bind;
    int joint_count;
    bool has_skin;

    /* animations */
    sk_animation_t *animations;
    int animation_count;
    int cur_anim;
    float anim_time;
    float anim_speed;
    bool anim_loop;

    sk_mat4_t joint_matrices[SK_MAX_JOINTS];

    vec3_t position;
    vec3_t rotation;
    vec3_t scale;
    sk_handle_t tint;
    bool visible;
} sk_model_data_t;

typedef struct {
    sk_handle_t model;
    sk_mat4_t mvp;
    sk_mat4_t model_mat;
    color_t tint;
} sk_model_draw_t;

typedef struct { float mvp[16]; float model[16]; } vs_params_t;
typedef struct { float mvp[16]; float model[16]; float joints[16 * SK_MAX_JOINTS]; } vs_skin_params_t;
typedef struct { float light_dir[4]; float tint[4]; float ambient[4]; } fs_params_t;

static sk_model_data_t sk_models[MAX_MODELS];
static sk_handle_pool_t sk_model_pool;
static uint16_t sk_model_free_indices[MAX_MODELS];
static uint16_t sk_model_generations[MAX_MODELS];
static unsigned char sk_model_occupied[MAX_MODELS];

static sg_pipeline sk_pip_static;
static sg_pipeline sk_pip_skinned;
static sg_shader sk_shd_static;
static sg_shader sk_shd_skinned;
static sg_sampler sk_model_sampler;
static sg_image sk_model_white_img;
static sg_view sk_model_white_view;

static sk_model_draw_t sk_draw_queue[MAX_DRAW_QUEUE];
static int sk_draw_count;

static void enqueue(sk_handle_t handle);
static bool model_bounds(sk_handle_t handle, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model);
static bool model_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out);

/* ------------------------------------------------------------- shaders ----- */

static const char *vs_static_src =
    "#version 410\n"
    "in vec3 position;\nin vec3 normal;\nin vec2 texcoord0;\n"
    "uniform mat4 mvp;\nuniform mat4 model;\n"
    "out vec3 v_normal;\nout vec2 v_uv;\n"
    "void main(){\n"
    "  gl_Position = mvp * vec4(position,1.0);\n"
    "  v_normal = mat3(model) * normal;\n"
    "  v_uv = texcoord0;\n"
    "}\n";

static const char *vs_skinned_src =
    "#version 410\n"
    "in vec3 position;\nin vec3 normal;\nin vec2 texcoord0;\n"
    "in vec4 joints;\nin vec4 weights;\n"
    "uniform mat4 mvp;\nuniform mat4 model;\n"
    "uniform mat4 joints_mat[128];\n"
    "out vec3 v_normal;\nout vec2 v_uv;\n"
    "void main(){\n"
    "  mat4 skin = weights.x*joints_mat[int(joints.x)]\n"
    "            + weights.y*joints_mat[int(joints.y)]\n"
    "            + weights.z*joints_mat[int(joints.z)]\n"
    "            + weights.w*joints_mat[int(joints.w)];\n"
    "  vec4 sp = skin * vec4(position,1.0);\n"
    "  gl_Position = mvp * sp;\n"
    "  v_normal = mat3(model) * mat3(skin) * normal;\n"
    "  v_uv = texcoord0;\n"
    "}\n";

static const char *fs_src =
    "#version 410\n"
    "in vec3 v_normal;\nin vec2 v_uv;\nout vec4 frag_color;\n"
    "uniform vec4 u_light_dir;\nuniform vec4 u_tint;\nuniform vec4 u_ambient;\n"
    "uniform sampler2D tex;\n"
    "void main(){\n"
    "  vec3 n = normalize(v_normal);\n"
    "  vec3 ld = normalize(u_light_dir.xyz);\n"
    "  float d = max(dot(n, -ld), 0.0);\n"
    "  float a = u_ambient.x;\n"
    "  float lit = a + (1.0-a)*d;\n"
    "  vec4 base = texture(tex, v_uv) * u_tint;\n"
    "  frag_color = vec4(base.rgb*lit, base.a);\n"
    "}\n";

static void fill_fs_shader(sg_shader_desc *d, int ub_slot)
{
    d->uniform_blocks[ub_slot].stage = SG_SHADERSTAGE_FRAGMENT;
    d->uniform_blocks[ub_slot].size = sizeof(fs_params_t);
    d->uniform_blocks[ub_slot].glsl_uniforms[0] = (sg_glsl_shader_uniform){
        .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_light_dir"};
    d->uniform_blocks[ub_slot].glsl_uniforms[1] = (sg_glsl_shader_uniform){
        .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_tint"};
    d->uniform_blocks[ub_slot].glsl_uniforms[2] = (sg_glsl_shader_uniform){
        .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "u_ambient"};
    d->views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    d->views[0].texture.image_type = SG_IMAGETYPE_2D;
    d->views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    d->samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    d->samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    d->texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    d->texture_sampler_pairs[0].view_slot = 0;
    d->texture_sampler_pairs[0].sampler_slot = 0;
    d->texture_sampler_pairs[0].glsl_name = "tex";
}

static sg_shader make_static_shader(void)
{
    sg_shader_desc d = {0};
    d.vertex_func.source = vs_static_src;
    d.fragment_func.source = fs_src;
    d.attrs[0].glsl_name = "position";
    d.attrs[1].glsl_name = "normal";
    d.attrs[2].glsl_name = "texcoord0";
    d.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    d.uniform_blocks[0].size = sizeof(vs_params_t);
    d.uniform_blocks[0].glsl_uniforms[0] = (sg_glsl_shader_uniform){.type = SG_UNIFORMTYPE_MAT4, .glsl_name = "mvp"};
    d.uniform_blocks[0].glsl_uniforms[1] = (sg_glsl_shader_uniform){.type = SG_UNIFORMTYPE_MAT4, .glsl_name = "model"};
    fill_fs_shader(&d, 1);
    d.label = "sk-model-static";
    return sg_make_shader(&d);
}

static sg_shader make_skinned_shader(void)
{
    sg_shader_desc d = {0};
    d.vertex_func.source = vs_skinned_src;
    d.fragment_func.source = fs_src;
    d.attrs[0].glsl_name = "position";
    d.attrs[1].glsl_name = "normal";
    d.attrs[2].glsl_name = "texcoord0";
    d.attrs[3].glsl_name = "joints";
    d.attrs[4].glsl_name = "weights";
    d.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    d.uniform_blocks[0].size = sizeof(vs_skin_params_t);
    d.uniform_blocks[0].glsl_uniforms[0] = (sg_glsl_shader_uniform){.type = SG_UNIFORMTYPE_MAT4, .glsl_name = "mvp"};
    d.uniform_blocks[0].glsl_uniforms[1] = (sg_glsl_shader_uniform){.type = SG_UNIFORMTYPE_MAT4, .glsl_name = "model"};
    d.uniform_blocks[0].glsl_uniforms[2] = (sg_glsl_shader_uniform){.type = SG_UNIFORMTYPE_MAT4, .array_count = SK_MAX_JOINTS, .glsl_name = "joints_mat"};
    fill_fs_shader(&d, 1);
    d.label = "sk-model-skinned";
    return sg_make_shader(&d);
}

/* ----------------------------------------------------------- gltf load ----- */

static sg_view decode_image_view(const cgltf_image *img)
{
    const cgltf_buffer_view *bv;
    const unsigned char *bytes;
    int w = 0, h = 0, comp = 0;
    stbi_uc *pixels;
    sg_image image;

    if (img == NULL || img->buffer_view == NULL || img->buffer_view->buffer == NULL ||
        img->buffer_view->buffer->data == NULL) {
        return sk_model_white_view;
    }
    bv = img->buffer_view;
    bytes = (const unsigned char *)bv->buffer->data + bv->offset;
    pixels = stbi_load_from_memory(bytes, (int)bv->size, &w, &h, &comp, 4);
    if (pixels == NULL) {
        return sk_model_white_view;
    }
    image = sg_make_image(&(sg_image_desc){
        .width = w, .height = h, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {.ptr = pixels, .size = (size_t)(w * h * 4)},
    });
    stbi_image_free(pixels);
    return sg_make_view(&(sg_view_desc){.texture.image = image});
}

static bool build_primitive(const cgltf_primitive *prim, bool skinned, sk_primitive_t *out)
{
    const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *jnt = NULL, *wgt = NULL;
    cgltf_size vcount, icount, i;
    float *verts;
    uint32_t *indices;
    float *positions;
    size_t stride = skinned ? 16 : 8;

    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        const cgltf_attribute *attr = &prim->attributes[a];
        switch (attr->type) {
            case cgltf_attribute_type_position: pos = attr->data; break;
            case cgltf_attribute_type_normal: nrm = attr->data; break;
            case cgltf_attribute_type_texcoord: if (!uv) uv = attr->data; break;
            case cgltf_attribute_type_joints: if (!jnt) jnt = attr->data; break;
            case cgltf_attribute_type_weights: if (!wgt) wgt = attr->data; break;
            default: break;
        }
    }
    if (pos == NULL) return false;
    if (skinned && (jnt == NULL || wgt == NULL)) skinned = false, stride = 8;

    vcount = pos->count;
    verts = (float *)calloc(vcount * stride, sizeof(float));
    if (verts == NULL) return false;
    positions = (float *)malloc(vcount * 3 * sizeof(float));
    if (positions == NULL) {
        free(verts);
        return false;
    }

    out->pmin = (vec3_t){1e30f, 1e30f, 1e30f};
    out->pmax = (vec3_t){-1e30f, -1e30f, -1e30f};

    for (i = 0; i < vcount; i++) {
        float p[3] = {0}, n[3] = {0, 1, 0}, t[2] = {0}, j[4] = {0}, w[4] = {0};
        float *v = &verts[i * stride];
        cgltf_accessor_read_float(pos, i, p, 3);
        positions[i * 3 + 0] = p[0];
        positions[i * 3 + 1] = p[1];
        positions[i * 3 + 2] = p[2];
        if (p[0] < out->pmin.x) out->pmin.x = p[0];
        if (p[1] < out->pmin.y) out->pmin.y = p[1];
        if (p[2] < out->pmin.z) out->pmin.z = p[2];
        if (p[0] > out->pmax.x) out->pmax.x = p[0];
        if (p[1] > out->pmax.y) out->pmax.y = p[1];
        if (p[2] > out->pmax.z) out->pmax.z = p[2];
        if (nrm) cgltf_accessor_read_float(nrm, i, n, 3);
        if (uv) cgltf_accessor_read_float(uv, i, t, 2);
        v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
        v[3] = n[0]; v[4] = n[1]; v[5] = n[2];
        v[6] = t[0]; v[7] = t[1];
        if (skinned) {
            cgltf_accessor_read_float(jnt, i, j, 4);
            cgltf_accessor_read_float(wgt, i, w, 4);
            v[8] = j[0]; v[9] = j[1]; v[10] = j[2]; v[11] = j[3];
            v[12] = w[0]; v[13] = w[1]; v[14] = w[2]; v[15] = w[3];
        }
    }

    if (prim->indices != NULL) {
        icount = prim->indices->count;
        indices = (uint32_t *)malloc(icount * sizeof(uint32_t));
        for (i = 0; i < icount; i++) indices[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
    } else {
        icount = vcount;
        indices = (uint32_t *)malloc(icount * sizeof(uint32_t));
        for (i = 0; i < icount; i++) indices[i] = (uint32_t)i;
    }

    out->vbuf = sg_make_buffer(&(sg_buffer_desc){
        .usage.vertex_buffer = true,
        .data = {.ptr = verts, .size = vcount * stride * sizeof(float)}});
    out->ibuf = sg_make_buffer(&(sg_buffer_desc){
        .usage.index_buffer = true,
        .data = {.ptr = indices, .size = icount * sizeof(uint32_t)}});
    out->index_count = (int)icount;
    out->pick_positions = positions;
    out->pick_indices = indices;
    out->pick_vertex_count = (int)vcount;
    out->sampler = sk_model_sampler;
    out->view = sk_model_white_view;
    out->skinned = skinned;
    out->base_color[0] = out->base_color[1] = out->base_color[2] = out->base_color[3] = 1.0f;

    if (prim->material != NULL && prim->material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness *pbr = &prim->material->pbr_metallic_roughness;
        memcpy(out->base_color, pbr->base_color_factor, sizeof(out->base_color));
        if (pbr->base_color_texture.texture != NULL && pbr->base_color_texture.texture->image != NULL) {
            out->view = decode_image_view(pbr->base_color_texture.texture->image);
        }
    }

    free(verts);
    /* `positions` and `indices` are retained on the primitive for picking */
    return true;
}

static int node_index(const cgltf_data *g, const cgltf_node *n)
{
    return n != NULL ? (int)(n - g->nodes) : -1;
}

static void node_local_trs(const cgltf_node *n, vec3_t *t, quat_t *r, vec3_t *s)
{
    *t = (vec3_t){0, 0, 0};
    *r = (quat_t){0, 0, 0, 1};
    *s = (vec3_t){1, 1, 1};
    if (n->has_matrix) {
        /* decompose column-major matrix into TRS */
        const float *m = n->matrix;
        float sx = sqrtf(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]);
        float sy = sqrtf(m[4]*m[4]+m[5]*m[5]+m[6]*m[6]);
        float sz = sqrtf(m[8]*m[8]+m[9]*m[9]+m[10]*m[10]);
        float r00, r11, r22, tr;
        *t = (vec3_t){m[12], m[13], m[14]};
        *s = (vec3_t){sx, sy, sz};
        if (sx > 1e-6f && sy > 1e-6f && sz > 1e-6f) {
            r00 = m[0]/sx; r11 = m[5]/sy; r22 = m[10]/sz;
            tr = r00 + r11 + r22;
            if (tr > 0.0f) {
                float qs = sqrtf(tr + 1.0f) * 2.0f;
                r->w = 0.25f * qs;
                r->x = (m[6]/sy - m[9]/sz) / qs;
                r->y = (m[8]/sz - m[2]/sx) / qs;
                r->z = (m[1]/sx - m[4]/sy) / qs;
            } else {
                r->w = 1.0f; r->x = r->y = r->z = 0.0f;
            }
        }
        return;
    }
    if (n->has_translation) *t = (vec3_t){n->translation[0], n->translation[1], n->translation[2]};
    if (n->has_rotation) *r = (quat_t){n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
    if (n->has_scale) *s = (vec3_t){n->scale[0], n->scale[1], n->scale[2]};
}

static void parse_skeleton(sk_model_data_t *md, const cgltf_data *g)
{
    const cgltf_skin *skin = NULL;

    /* nodes */
    md->node_count = (int)g->nodes_count;
    md->nodes = (sk_node_t *)calloc((size_t)md->node_count, sizeof(sk_node_t));
    for (int i = 0; i < md->node_count; i++) {
        const cgltf_node *n = &g->nodes[i];
        node_local_trs(n, &md->nodes[i].t, &md->nodes[i].r, &md->nodes[i].s);
        md->nodes[i].parent = node_index(g, n->parent);
    }

    /* first skin */
    for (cgltf_size i = 0; i < g->nodes_count && skin == NULL; i++) {
        if (g->nodes[i].skin) skin = g->nodes[i].skin;
    }
    if (skin == NULL) return;

    md->joint_count = (int)skin->joints_count;
    if (md->joint_count > SK_MAX_JOINTS) {
        log_warn("model has %d joints; clamping to %d", md->joint_count, SK_MAX_JOINTS);
        md->joint_count = SK_MAX_JOINTS;
    }
    md->joint_nodes = (int *)calloc((size_t)md->joint_count, sizeof(int));
    md->inverse_bind = (sk_mat4_t *)calloc((size_t)md->joint_count, sizeof(sk_mat4_t));
    for (int j = 0; j < md->joint_count; j++) {
        md->joint_nodes[j] = node_index(g, skin->joints[j]);
        if (skin->inverse_bind_matrices) {
            cgltf_accessor_read_float(skin->inverse_bind_matrices, (cgltf_size)j,
                                      md->inverse_bind[j].m, 16);
        } else {
            md->inverse_bind[j] = sk_mat4_identity();
        }
    }
    md->has_skin = true;
}

static void parse_animations(sk_model_data_t *md, const cgltf_data *g)
{
    md->animation_count = (int)g->animations_count;
    if (md->animation_count == 0) return;
    md->animations = (sk_animation_t *)calloc((size_t)md->animation_count, sizeof(sk_animation_t));

    for (int a = 0; a < md->animation_count; a++) {
        const cgltf_animation *ga = &g->animations[a];
        sk_animation_t *anim = &md->animations[a];
        int cc = 0;

        anim->channels = (sk_anim_channel_t *)calloc(ga->channels_count, sizeof(sk_anim_channel_t));
        for (cgltf_size c = 0; c < ga->channels_count; c++) {
            const cgltf_animation_channel *gc = &ga->channels[c];
            const cgltf_animation_sampler *gs = gc->sampler;
            sk_anim_channel_t *ch = &anim->channels[cc];
            int comp;

            if (gc->target_path == cgltf_animation_path_type_translation) ch->path = 0;
            else if (gc->target_path == cgltf_animation_path_type_rotation) ch->path = 1;
            else if (gc->target_path == cgltf_animation_path_type_scale) ch->path = 2;
            else continue; /* skip weights/morph */

            ch->node = node_index(g, gc->target_node);
            ch->interp = (gs->interpolation == cgltf_interpolation_type_step) ? 1 : 0;
            ch->key_count = (int)gs->input->count;
            comp = (ch->path == 1) ? 4 : 3;

            ch->times = (float *)malloc((size_t)ch->key_count * sizeof(float));
            ch->values = (float *)malloc((size_t)ch->key_count * comp * sizeof(float));
            for (int k = 0; k < ch->key_count; k++) {
                cgltf_accessor_read_float(gs->input, (cgltf_size)k, &ch->times[k], 1);
                cgltf_accessor_read_float(gs->output, (cgltf_size)k, &ch->values[k * comp], comp);
                if (ch->times[k] > anim->duration) anim->duration = ch->times[k];
            }
            cc++;
        }
        anim->channel_count = cc;
    }
}

static bool load_model(sk_model_data_t *md, const unsigned char *data, int size)
{
    cgltf_options options = {0};
    cgltf_data *g = NULL;
    int total = 0, idx = 0;

    if (cgltf_parse(&options, data, (cgltf_size)size, &g) != cgltf_result_success) return false;
    if (cgltf_load_buffers(&options, g, NULL) != cgltf_result_success) { cgltf_free(g); return false; }

    parse_skeleton(md, g);
    parse_animations(md, g);

    /* count triangle primitives across nodes that carry a mesh */
    for (cgltf_size n = 0; n < g->nodes_count; n++) {
        if (g->nodes[n].mesh) total += (int)g->nodes[n].mesh->primitives_count;
    }
    if (total == 0) {
        /* fall back to meshes not referenced by nodes */
        for (cgltf_size m = 0; m < g->meshes_count; m++) total += (int)g->meshes[m].primitives_count;
    }
    if (total == 0) { cgltf_free(g); return false; }
    md->prims = (sk_primitive_t *)calloc((size_t)total, sizeof(sk_primitive_t));

    for (cgltf_size n = 0; n < g->nodes_count; n++) {
        const cgltf_node *node = &g->nodes[n];
        if (node->mesh == NULL) continue;
        bool node_skinned = (node->skin != NULL) && md->has_skin;
        for (cgltf_size p = 0; p < node->mesh->primitives_count; p++) {
            if (node->mesh->primitives[p].type != cgltf_primitive_type_triangles) continue;
            if (build_primitive(&node->mesh->primitives[p], node_skinned, &md->prims[idx])) idx++;
        }
    }
    if (idx == 0) { /* no node-meshes: load meshes directly (static) */
        for (cgltf_size m = 0; m < g->meshes_count; m++) {
            for (cgltf_size p = 0; p < g->meshes[m].primitives_count; p++) {
                if (g->meshes[m].primitives[p].type != cgltf_primitive_type_triangles) continue;
                if (build_primitive(&g->meshes[m].primitives[p], false, &md->prims[idx])) idx++;
            }
        }
    }
    md->prim_count = idx;

    for (int j = 0; j < SK_MAX_JOINTS; j++) md->joint_matrices[j] = sk_mat4_identity();

    cgltf_free(g);
    return idx > 0;
}

/* ----------------------------------------------------------- animation ---- */

static void sample_channel(const sk_anim_channel_t *ch, float time, vec3_t *t, quat_t *r, vec3_t *s)
{
    int k0 = 0, k1 = 0, comp = (ch->path == 1) ? 4 : 3;
    float f = 0.0f;

    if (ch->key_count <= 0) return;
    if (time <= ch->times[0]) {
        k0 = k1 = 0;
    } else if (time >= ch->times[ch->key_count - 1]) {
        k0 = k1 = ch->key_count - 1;
    } else {
        for (int k = 0; k < ch->key_count - 1; k++) {
            if (time >= ch->times[k] && time < ch->times[k + 1]) {
                k0 = k; k1 = k + 1;
                float dt = ch->times[k1] - ch->times[k0];
                f = dt > 1e-8f ? (time - ch->times[k0]) / dt : 0.0f;
                break;
            }
        }
    }
    if (ch->interp == 1) f = 0.0f; /* step */

    const float *v0 = &ch->values[k0 * comp];
    const float *v1 = &ch->values[k1 * comp];
    if (ch->path == 0) {
        *t = sk_v3_lerp((vec3_t){v0[0], v0[1], v0[2]}, (vec3_t){v1[0], v1[1], v1[2]}, f);
    } else if (ch->path == 2) {
        *s = sk_v3_lerp((vec3_t){v0[0], v0[1], v0[2]}, (vec3_t){v1[0], v1[1], v1[2]}, f);
    } else {
        *r = sk_quat_slerp((quat_t){v0[0], v0[1], v0[2], v0[3]},
                           (quat_t){v1[0], v1[1], v1[2], v1[3]}, f);
    }
}

/* recursive global transform with per-call cache */
static sk_mat4_t global_of(sk_model_data_t *md, vec3_t *ct, quat_t *cr, vec3_t *cs,
                           sk_mat4_t *cache, bool *done, int i)
{
    sk_mat4_t local;
    if (done[i]) return cache[i];
    local = sk_mat4_compose(ct[i], cr[i], cs[i]);
    if (md->nodes[i].parent >= 0) {
        cache[i] = sk_mat4_mul(global_of(md, ct, cr, cs, cache, done, md->nodes[i].parent), local);
    } else {
        cache[i] = local;
    }
    done[i] = true;
    return cache[i];
}

SK_KEEP
bool sk_model_animate(sk_handle_t handle, float delta_seconds)
{
    sk_model_data_t *md;
    sk_animation_t *anim;
    vec3_t *ct, *cs;
    quat_t *cr;
    sk_mat4_t *cache;
    bool *done;
    {
        uint16_t index = 0;
        if (!sk_handle_pool_resolve(&sk_model_pool, handle, &index)) return false;
        md = &sk_models[index];
    }
    if (!md->has_skin || md->cur_anim < 0 || md->cur_anim >= md->animation_count) return false;
    anim = &md->animations[md->cur_anim];

    md->anim_time += delta_seconds * md->anim_speed;
    if (anim->duration > 0.0f) {
        if (md->anim_loop) {
            md->anim_time = fmodf(md->anim_time, anim->duration);
            if (md->anim_time < 0.0f) md->anim_time += anim->duration;
        } else if (md->anim_time > anim->duration) {
            md->anim_time = anim->duration;
        }
    }

    ct = (vec3_t *)malloc((size_t)md->node_count * sizeof(vec3_t));
    cr = (quat_t *)malloc((size_t)md->node_count * sizeof(quat_t));
    cs = (vec3_t *)malloc((size_t)md->node_count * sizeof(vec3_t));
    cache = (sk_mat4_t *)malloc((size_t)md->node_count * sizeof(sk_mat4_t));
    done = (bool *)calloc((size_t)md->node_count, sizeof(bool));

    for (int i = 0; i < md->node_count; i++) {
        ct[i] = md->nodes[i].t; cr[i] = md->nodes[i].r; cs[i] = md->nodes[i].s;
    }
    for (int c = 0; c < anim->channel_count; c++) {
        sk_anim_channel_t *ch = &anim->channels[c];
        if (ch->node < 0 || ch->node >= md->node_count) continue;
        sample_channel(ch, md->anim_time, &ct[ch->node], &cr[ch->node], &cs[ch->node]);
    }
    for (int j = 0; j < md->joint_count; j++) {
        int jn = md->joint_nodes[j];
        sk_mat4_t gjoint = global_of(md, ct, cr, cs, cache, done, jn);
        md->joint_matrices[j] = sk_mat4_mul(gjoint, md->inverse_bind[j]);
    }

    free(ct); free(cr); free(cs); free(cache); free(done);
    return true;
}

SK_KEEP int sk_model_get_animation_count(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_model_pool, handle, &index)) return 0;
    return sk_models[index].animation_count;
}

SK_KEEP bool sk_model_set_animation(sk_handle_t handle, int animation_index)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_model_pool, handle, &index)) return false;
    if (animation_index < 0 || animation_index >= sk_models[index].animation_count) return false;
    sk_models[index].cur_anim = animation_index;
    sk_models[index].anim_time = 0.0f;
    return true;
}

SK_KEEP bool sk_model_set_animation_speed(sk_handle_t handle, float speed)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_model_pool, handle, &index)) return false;
    sk_models[index].anim_speed = speed;
    return true;
}

SK_KEEP bool sk_model_set_animation_loop(sk_handle_t handle, bool loop)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_model_pool, handle, &index)) return false;
    sk_models[index].anim_loop = loop;
    return true;
}

/* ----------------------------------------------------------- public API ---- */

static sk_model_data_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_model_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid model handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_models[index];
}

SK_KEEP
sk_handle_t sk_model_create_from_memory(const unsigned char *data, int size, const char *hint)
{
    sk_handle_t handle;
    uint16_t index = 0;
    sk_model_data_t md = {0};

    (void)hint;
    md.scale = (vec3_t){1, 1, 1};
    md.visible = true;
    md.cur_anim = -1;
    md.anim_speed = 1.0f;
    md.anim_loop = true;
    if (!load_model(&md, data, size)) {
        log_error("failed to load model");
        return 0;
    }
    handle = sk_handle_pool_alloc(&sk_model_pool);
    if (handle == 0) {
        log_error("MAX_MODELS reached (%d)", MAX_MODELS);
        return 0;
    }
    sk_handle_pool_resolve(&sk_model_pool, handle, &index);
    sk_models[index] = md;
    return handle;
}

SK_KEEP
sk_handle_t sk_model_create(const char *path)
{
    FILE *f;
    long size;
    unsigned char *bytes;
    sk_handle_t handle;

    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        log_error("failed to open model: %s", path ? path : "(null)");
        return 0;
    }
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return 0; }
    bytes = (unsigned char *)malloc((size_t)size);
    if (bytes == NULL) { fclose(f); return 0; }
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size) { free(bytes); fclose(f); return 0; }
    fclose(f);
    handle = sk_model_create_from_memory(bytes, (int)size, path);
    free(bytes);
    return handle;
}

SK_KEEP bool sk_model_set_transform(sk_handle_t handle,
                                    float px, float py, float pz,
                                    float rx, float ry, float rz,
                                    float sx, float sy, float sz)
{
    sk_model_data_t *md = resolve(handle);
    if (md == NULL) return false;
    md->position = (vec3_t){px, py, pz};
    md->rotation = (vec3_t){rx, ry, rz};
    md->scale = (vec3_t){sx, sy, sz};
    return true;
}

SK_KEEP bool sk_model_set_tint(sk_handle_t handle, sk_handle_t color)
{
    sk_model_data_t *md = resolve(handle);
    if (md == NULL) return false;
    md->tint = color;
    return true;
}

SK_KEEP bool sk_model_set_visible(sk_handle_t handle, bool visible)
{
    sk_model_data_t *md = resolve(handle);
    if (md == NULL) return false;
    md->visible = visible;
    return true;
}

SK_KEEP bool sk_model_is_visible(sk_handle_t handle)
{
    sk_model_data_t *md = resolve(handle);
    return md != NULL && md->visible;
}

SK_KEEP void sk_model_draw(sk_handle_t handle) { enqueue(handle); }

static bool model_bounds(sk_handle_t handle, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model)
{
    sk_model_data_t *md = resolve(handle);
    vec3_t mn = {1e30f, 1e30f, 1e30f}, mx = {-1e30f, -1e30f, -1e30f};
    if (md == NULL || !md->visible || md->prim_count == 0) {
        return false;
    }
    for (int p = 0; p < md->prim_count; p++) {
        if (md->prims[p].pmin.x < mn.x) mn.x = md->prims[p].pmin.x;
        if (md->prims[p].pmin.y < mn.y) mn.y = md->prims[p].pmin.y;
        if (md->prims[p].pmin.z < mn.z) mn.z = md->prims[p].pmin.z;
        if (md->prims[p].pmax.x > mx.x) mx.x = md->prims[p].pmax.x;
        if (md->prims[p].pmax.y > mx.y) mx.y = md->prims[p].pmax.y;
        if (md->prims[p].pmax.z > mx.z) mx.z = md->prims[p].pmax.z;
    }
    *lmin = mn;
    *lmax = mx;
    *model = sk_mat4_trs(md->position, md->rotation, md->scale);
    return true;
}

static bool model_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out)
{
    sk_model_data_t *md = resolve(handle);
    sk_mat4_t model;
    sk_ray_t world, local;
    sk_ray_hit_t best = {0};
    vec3_t lmin, lmax;

    if (out == NULL || md == NULL || !md->visible || md->prim_count == 0) {
        return false;
    }

    if (!model_bounds(handle, &lmin, &lmax, &model)) {
        return false;
    }

    world.origin = origin;
    world.dir = dir;
    local = sk_pick_ray_to_local(model, world);

    /* Narrow phase: exact ray/triangle against retained bind-pose geometry.
     * Skinned meshes are tested against the bind pose (matches raylib). */
    for (int p = 0; p < md->prim_count; p++) {
        sk_primitive_t *prim = &md->prims[p];
        if (prim->pick_positions == NULL || prim->pick_indices == NULL) {
            continue;
        }
        for (int k = 0; k + 2 < prim->index_count; k += 3) {
            uint32_t i0 = prim->pick_indices[k];
            uint32_t i1 = prim->pick_indices[k + 1];
            uint32_t i2 = prim->pick_indices[k + 2];
            vec3_t v0 = {prim->pick_positions[i0 * 3 + 0],
                         prim->pick_positions[i0 * 3 + 1],
                         prim->pick_positions[i0 * 3 + 2]};
            vec3_t v1 = {prim->pick_positions[i1 * 3 + 0],
                         prim->pick_positions[i1 * 3 + 1],
                         prim->pick_positions[i1 * 3 + 2]};
            vec3_t v2 = {prim->pick_positions[i2 * 3 + 0],
                         prim->pick_positions[i2 * 3 + 1],
                         prim->pick_positions[i2 * 3 + 2]};
            sk_ray_hit_t th = {0};
            if (sk_pick_ray_triangle(local, v0, v1, v2, &th) &&
                (!best.hit || th.t < best.t)) {
                best = th;
            }
        }
    }

    if (best.hit) {
        sk_pick_result_from_local(&best, world, model, out);
    } else {
        *out = (sk_pick_result_t){0};
    }
    return true;
}

static void enqueue(sk_handle_t handle)
{
    sk_model_data_t *md = resolve(handle);
    sk_camera3d_data_t cam;
    sk_mat4_t model_mat, view, proj, vp;
    float aspect;
    sk_model_draw_t *e;

    if (md == NULL || !md->visible || md->prim_count == 0) return;
    if (sk_draw_count >= MAX_DRAW_QUEUE) return;
    if (!sk_camera3d_get_active_data(&cam)) return;

    aspect = sapp_height() > 0 ? (float)sapp_width() / (float)sapp_height() : 1.0f;
    model_mat = sk_mat4_trs(md->position, md->rotation, md->scale);
    view = sk_mat4_lookat(cam.position, cam.target, cam.up);
    proj = sk_mat4_perspective(cam.fovy * 0.01745329252f, aspect, 0.01f, 1000.0f);
    vp = sk_mat4_mul(proj, view);

    e = &sk_draw_queue[sk_draw_count++];
    e->model = handle;
    e->mvp = sk_mat4_mul(vp, model_mat);
    e->model_mat = model_mat;
    e->tint = sk_color_get(md->tint != 0 ? md->tint : 0);
}

static void apply_fs(sk_model_draw_t *e, sk_primitive_t *prim)
{
    fs_params_t fsp;
    fsp.light_dir[0] = -0.6f; fsp.light_dir[1] = -1.0f; fsp.light_dir[2] = -0.5f; fsp.light_dir[3] = 0.0f;
    fsp.tint[0] = e->tint.r * prim->base_color[0];
    fsp.tint[1] = e->tint.g * prim->base_color[1];
    fsp.tint[2] = e->tint.b * prim->base_color[2];
    fsp.tint[3] = e->tint.a * prim->base_color[3];
    fsp.ambient[0] = 0.3f; fsp.ambient[1] = fsp.ambient[2] = fsp.ambient[3] = 0.0f;
    sg_apply_uniforms(1, &(sg_range){.ptr = &fsp, .size = sizeof(fsp)});
}

void sk_model_flush(void)
{
    int cur_pip = 0; /* 0 none, 1 static, 2 skinned */

    for (int i = 0; i < sk_draw_count; i++) {
        sk_model_draw_t *e = &sk_draw_queue[i];
        sk_model_data_t *md = resolve(e->model);
        if (md == NULL) continue;

        for (int p = 0; p < md->prim_count; p++) {
            sk_primitive_t *prim = &md->prims[p];
            int want = prim->skinned ? 2 : 1;
            if (want != cur_pip) {
                sg_apply_pipeline(prim->skinned ? sk_pip_skinned : sk_pip_static);
                cur_pip = want;
            }

            if (prim->skinned) {
                vs_skin_params_t vsp;
                memcpy(vsp.mvp, e->mvp.m, sizeof(vsp.mvp));
                memcpy(vsp.model, e->model_mat.m, sizeof(vsp.model));
                for (int j = 0; j < SK_MAX_JOINTS; j++) {
                    memcpy(&vsp.joints[j * 16], md->joint_matrices[j].m, 16 * sizeof(float));
                }
                sg_apply_uniforms(0, &(sg_range){.ptr = &vsp, .size = sizeof(vsp)});
            } else {
                vs_params_t vsp;
                memcpy(vsp.mvp, e->mvp.m, sizeof(vsp.mvp));
                memcpy(vsp.model, e->model_mat.m, sizeof(vsp.model));
                sg_apply_uniforms(0, &(sg_range){.ptr = &vsp, .size = sizeof(vsp)});
            }

            sg_apply_bindings(&(sg_bindings){
                .vertex_buffers[0] = prim->vbuf,
                .index_buffer = prim->ibuf,
                .views[0] = prim->view,
                .samplers[0] = prim->sampler,
            });
            apply_fs(e, prim);
            sg_draw(0, prim->index_count, 1);
        }
    }
    sk_draw_count = 0;
}

static void free_model_cpu(sk_model_data_t *md)
{
    for (int a = 0; a < md->animation_count; a++) {
        for (int c = 0; c < md->animations[a].channel_count; c++) {
            free(md->animations[a].channels[c].times);
            free(md->animations[a].channels[c].values);
        }
        free(md->animations[a].channels);
    }
    free(md->animations);
    free(md->nodes);
    free(md->joint_nodes);
    free(md->inverse_bind);
}

SK_KEEP void sk_model_destroy(sk_handle_t handle)
{
    sk_model_data_t *md = resolve(handle);
    if (md == NULL) return;
    for (int p = 0; p < md->prim_count; p++) {
        sg_destroy_buffer(md->prims[p].vbuf);
        sg_destroy_buffer(md->prims[p].ibuf);
        if (md->prims[p].view.id != sk_model_white_view.id) sg_destroy_view(md->prims[p].view);
        free(md->prims[p].pick_positions);
        free(md->prims[p].pick_indices);
    }
    free(md->prims);
    free_model_cpu(md);
    memset(md, 0, sizeof(*md));
    sk_handle_pool_free(&sk_model_pool, handle);
}

void sk_model_init(void)
{
    static const unsigned char white[4] = {255, 255, 255, 255};
    sg_pipeline_desc base;

    memset(sk_models, 0, sizeof(sk_models));
    sk_draw_count = 0;
    sk_handle_pool_init(&sk_model_pool, SK_HANDLE_KIND_MODEL, MAX_MODELS,
                        sk_model_free_indices, MAX_MODELS,
                        sk_model_generations, sk_model_occupied);

    sk_shd_static = make_static_shader();
    sk_shd_skinned = make_skinned_shader();

    base = (sg_pipeline_desc){
        .index_type = SG_INDEXTYPE_UINT32,
        .cull_mode = SG_CULLMODE_BACK,
        .face_winding = SG_FACEWINDING_CCW,
        .depth = {.compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true},
    };

    {
        sg_pipeline_desc d = base;
        d.shader = sk_shd_static;
        d.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        d.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
        d.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
        d.label = "sk-model-pip-static";
        sk_pip_static = sg_make_pipeline(&d);
    }
    {
        sg_pipeline_desc d = base;
        d.shader = sk_shd_skinned;
        d.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        d.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
        d.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
        d.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4;
        d.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT4;
        d.label = "sk-model-pip-skinned";
        sk_pip_skinned = sg_make_pipeline(&d);
    }

    sk_model_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .mipmap_filter = SG_FILTER_LINEAR, .wrap_u = SG_WRAP_REPEAT, .wrap_v = SG_WRAP_REPEAT});
    sk_model_white_img = sg_make_image(&(sg_image_desc){
        .width = 1, .height = 1, .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {.ptr = white, .size = sizeof(white)}});
    sk_model_white_view = sg_make_view(&(sg_view_desc){.texture.image = sk_model_white_img});

    sk_scene_register_drawable(SK_HANDLE_KIND_MODEL, enqueue);
    sk_scene_register_bounds(SK_HANDLE_KIND_MODEL, model_bounds);
    sk_scene_register_pick(SK_HANDLE_KIND_MODEL, model_pick);
}

void sk_model_deinit(void)
{
    for (uint16_t i = 1; i < MAX_MODELS; i++) {
        if (sk_model_occupied[i]) {
            sk_handle_t h = sk_handle_pool_handle_from_index(&sk_model_pool, i);
            sk_model_destroy(h);
        }
    }
    sg_destroy_view(sk_model_white_view);
    sg_destroy_image(sk_model_white_img);
    sg_destroy_sampler(sk_model_sampler);
    sg_destroy_pipeline(sk_pip_static);
    sg_destroy_pipeline(sk_pip_skinned);
    sg_destroy_shader(sk_shd_static);
    sg_destroy_shader(sk_shd_skinned);
    sk_handle_pool_reset(&sk_model_pool);
}
