#include "sk_shader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_loader.h"
#include "internal/sk_module.h"
#include "internal/sk_shader.h"
#include "sk_logger.h"

/* Custom material shaders: .skshader files from tools/shaderpack.py (the format is
 * written there). Loading reads the file on a worker; finishing picks the running
 * backend's sources and makes the static and skinned programs. */

#define SHADERS_INITIAL 8
#define FORMAT_VERSION 1

static sk_shader_t *sk_shaders; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_shader_pool;

static sk_shader_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_shader_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid shader handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_shaders[index];
}

sk_shader_t *sk_shader_get(sk_handle_t shader)
{
    uint16_t index = 0;
    return shader != 0 && sk_handle_pool_resolve(&sk_shader_pool, shader, &index) ? &sk_shaders[index] : NULL;
}

int sk_shader_find_param(const sk_shader_t *shader, const char *name)
{
    for (int i = 0; shader != NULL && name != NULL && i < shader->param_count; i++) {
        if (strcmp(shader->params[i].name, name) == 0) return i;
    }
    return -1;
}

int sk_shader_find_texture(const sk_shader_t *shader, const char *name)
{
    for (int i = 0; shader != NULL && name != NULL && i < shader->texture_count; i++) {
        if (strcmp(shader->textures[i], name) == 0) return i;
    }
    return -1;
}

/* ------------------------------------------------------------- parsing ---- */

typedef struct {
    const char *at, *end;
} reader_t;

/* The next line (without its newline) into `line`; false at the end. */
static bool next_line(reader_t *r, char *line, size_t size)
{
    const char *nl;
    size_t n;
    if (r->at >= r->end) return false;
    nl = memchr(r->at, '\n', (size_t)(r->end - r->at));
    n = (size_t)((nl != NULL ? nl : r->end) - r->at);
    snprintf(line, size, "%.*s", (int)(n < size ? n : size - 1), r->at);
    r->at = nl != NULL ? nl + 1 : r->end;
    return true;
}

static sg_shader_stage stage_of(const char *s)
{
    return strcmp(s, "vertex") == 0 ? SG_SHADERSTAGE_VERTEX : SG_SHADERSTAGE_FRAGMENT;
}

/* The name the tool uses for this backend's sources; NULL when there are none. */
static const char *backend_slang(void)
{
    switch (sg_query_backend()) {
        case SG_BACKEND_GLCORE: return "glsl410";
        case SG_BACKEND_GLES3: return "glsl300es";
        case SG_BACKEND_WGPU: return "wgsl";
        case SG_BACKEND_DUMMY: return "glsl410"; /* validates the layout only */
        default: return NULL;
    }
}

typedef struct {
    sg_shader_desc desc;
    char *sources[2];          /* vertex, fragment: NUL-terminated copies */
    char names[64][SK_SHADER_NAME_MAX]; /* glsl names the desc points at */
    int name_count;
    char view_names[SG_MAX_VIEW_BINDSLOTS][SK_SHADER_NAME_MAX];
    int pair_view[SG_MAX_TEXTURE_SAMPLER_PAIRS];
    int pair_sampler[SG_MAX_TEXTURE_SAMPLER_PAIRS];
    int pair_count;
    bool has_block[SK_SHADER_BLOCK_COUNT];
    bool found;
} program_desc_t;

static const char *keep_name(program_desc_t *p, const char *name)
{
    if (strcmp(name, "-") == 0 || p->name_count >= 64) return NULL;
    snprintf(p->names[p->name_count], SK_SHADER_NAME_MAX, "%.*s", SK_SHADER_NAME_MAX - 1, name);
    return p->names[p->name_count++];
}

/* One description line of a program (attr, ub, view, sampler, pair). */
static bool describe(program_desc_t *p, const char *line)
{
    char a[64], b[64], c[64], d[64];
    int slot, n1, n2, n3;
    sg_shader_desc *desc = &p->desc;

    if (sscanf(line, "attr %d %63s", &slot, a) == 2) {
        if (slot < 0 || slot >= SG_MAX_VERTEX_ATTRIBUTES) return false;
        desc->attrs[slot].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        desc->attrs[slot].glsl_name = keep_name(p, a);
        return true;
    }
    if (sscanf(line, "ub %d %63s %d %63s %d %d", &slot, a, &n1, b, &n2, &n3) == 6) {
        if (slot < 0 || slot >= SK_SHADER_BLOCK_COUNT) return false;
        desc->uniform_blocks[slot].stage = stage_of(a);
        desc->uniform_blocks[slot].layout = SG_UNIFORMLAYOUT_STD140;
        desc->uniform_blocks[slot].size = (uint32_t)n1;
        if (strcmp(b, "-") != 0) {
            desc->uniform_blocks[slot].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
            desc->uniform_blocks[slot].glsl_uniforms[0].array_count = (uint16_t)n2;
            desc->uniform_blocks[slot].glsl_uniforms[0].glsl_name = keep_name(p, b);
        }
        desc->uniform_blocks[slot].wgsl_group0_binding_n = (uint8_t)n3;
        p->has_block[slot] = true;
        return true;
    }
    if (sscanf(line, "view %d %63s %63s %63s %d", &slot, a, b, c, &n1) == 5) {
        if (slot < 0 || slot >= SK_SHADER_MAX_TEXTURES || strcmp(c, "float") != 0) return false;
        desc->views[slot].texture.stage = stage_of(a);
        desc->views[slot].texture.image_type = SG_IMAGETYPE_2D;
        desc->views[slot].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        desc->views[slot].texture.wgsl_group1_binding_n = (uint8_t)n1;
        snprintf(p->view_names[slot], SK_SHADER_NAME_MAX, "%.*s", SK_SHADER_NAME_MAX - 1, b);
        return true;
    }
    if (sscanf(line, "sampler %d %63s %63s %63s %d", &slot, a, b, c, &n1) == 5) {
        if (slot < 0 || slot >= SK_SHADER_MAX_TEXTURES) return false;
        desc->samplers[slot].stage = stage_of(a);
        desc->samplers[slot].sampler_type = strcmp(c, "nonfiltering") == 0 ? SG_SAMPLERTYPE_NONFILTERING
                                                                            : SG_SAMPLERTYPE_FILTERING;
        desc->samplers[slot].wgsl_group1_binding_n = (uint8_t)n1;
        return true;
    }
    if (sscanf(line, "pair %d %63s %d %d %63s", &slot, a, &n1, &n2, d) == 5) {
        if (slot < 0 || slot >= SG_MAX_TEXTURE_SAMPLER_PAIRS) return false;
        desc->texture_sampler_pairs[slot].stage = stage_of(a);
        desc->texture_sampler_pairs[slot].view_slot = (uint8_t)n1;
        desc->texture_sampler_pairs[slot].sampler_slot = (uint8_t)n2;
        desc->texture_sampler_pairs[slot].glsl_name = keep_name(p, d);
        if (p->pair_count < SG_MAX_TEXTURE_SAMPLER_PAIRS) {
            p->pair_view[p->pair_count] = n1;
            p->pair_sampler[p->pair_count++] = n2;
        }
        return true;
    }
    return false;
}

static sk_shader_param_type_t param_type(const char *s, bool *ok)
{
    *ok = true;
    if (strcmp(s, "float") == 0) return SK_SHADER_PARAM_FLOAT;
    if (strcmp(s, "int") == 0) return SK_SHADER_PARAM_INT;
    if (strcmp(s, "vec2") == 0) return SK_SHADER_PARAM_VEC2;
    if (strcmp(s, "vec3") == 0) return SK_SHADER_PARAM_VEC3;
    if (strcmp(s, "vec4") == 0) return SK_SHADER_PARAM_VEC4;
    *ok = false;
    return SK_SHADER_PARAM_FLOAT;
}

static int param_size(sk_shader_param_type_t type)
{
    switch (type) {
        case SK_SHADER_PARAM_VEC2: return 8;
        case SK_SHADER_PARAM_VEC3: return 12;
        case SK_SHADER_PARAM_VEC4: return 16;
        default: return 4;
    }
}

/* Read a .skshader: parameters and textures into `out`, and the two programs for
 * `slang` into `programs`. False (with *error) when the file can't be used. */
static bool parse(const unsigned char *bytes, size_t size, const char *slang, sk_shader_t *out,
                  program_desc_t programs[2], const char **error)
{
    reader_t r = {(const char *)bytes, (const char *)bytes + size};
    char line[512], a[64], b[64], c[64];
    int version = 0, n;
    program_desc_t *current = NULL;

    if (!next_line(&r, line, sizeof(line)) || sscanf(line, "skshader %d", &version) != 1) {
        *error = "not a .skshader file";
        return false;
    }
    if (version != FORMAT_VERSION) {
        *error = "made by a different version of tools/shaderpack.py (rebuild it)";
        return false;
    }
    while (next_line(&r, line, sizeof(line))) {
        if (strcmp(line, "end") == 0) {
            break;
        } else if (sscanf(line, "param %63s %63s %63s %d", a, b, c, &n) == 4) {
            bool ok;
            sk_shader_param_t *param = &out->params[out->param_count];
            if (out->param_count >= SK_SHADER_MAX_PARAMS || strlen(a) >= SK_SHADER_NAME_MAX) {
                *error = "too many parameters, or a name too long";
                return false;
            }
            snprintf(param->name, sizeof(param->name), "%s", a);
            param->type = param_type(b, &ok);
            param->block = strcmp(c, "vs") == 0 ? SK_SHADER_BLOCK_VS_PARAMS : SK_SHADER_BLOCK_FS_PARAMS;
            param->offset = n;
            if (!ok || n < 0) {
                *error = "a parameter of an unknown type";
                return false;
            }
            const int end = (n + param_size(param->type) + 15) / 16 * 16;
            if (end > out->block_size[param->block]) out->block_size[param->block] = end;
            out->param_count++;
        } else if (sscanf(line, "texture %63s", a) == 1) {
            if (out->texture_count >= SK_SHADER_MAX_TEXTURES || strlen(a) >= SK_SHADER_NAME_MAX) {
                *error = "too many textures, or a name too long";
                return false;
            }
            snprintf(out->textures[out->texture_count++], SK_SHADER_NAME_MAX, "%s", a);
        } else if (sscanf(line, "program %63s %63s", a, b) == 2) {
            const int which = strcmp(a, "static") == 0 ? 0 : (strcmp(a, "skinned") == 0 ? 1 : -1);
            current = which >= 0 && strcmp(b, slang) == 0 ? &programs[which] : NULL;
            if (current != NULL) current->found = true;
        } else if (sscanf(line, "source %63s %d", a, &n) == 2) {
            if (n < 0 || (size_t)n > (size_t)(r.end - r.at)) {
                *error = "a source is cut short";
                return false;
            }
            if (current != NULL) {
                char **source = &current->sources[strcmp(a, "vs") == 0 ? 0 : 1];
                free(*source);
                *source = malloc((size_t)n + 1);
                if (*source == NULL) {
                    *error = "out of memory";
                    return false;
                }
                memcpy(*source, r.at, (size_t)n);
                (*source)[n] = '\0';
            }
            r.at += n;
            if (r.at < r.end && *r.at == '\n') r.at++;
        } else if (current != NULL && !describe(current, line)) {
            *error = "a line it doesn't understand";
            return false;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (!programs[i].found || programs[i].sources[0] == NULL || programs[i].sources[1] == NULL) {
            *error = "no program for this graphics backend";
            return false;
        }
    }
    return true;
}

static void free_programs(program_desc_t programs[2])
{
    for (int i = 0; i < 2; i++) {
        free(programs[i].sources[0]);
        free(programs[i].sources[1]);
    }
}

static void destroy_gpu(sk_shader_t *shader)
{
    for (int s = 0; s < 2; s++) {
        for (int b = 0; b < 2; b++) {
            for (int d = 0; d < 2; d++) {
                if (shader->pipelines[s][b][d].id != SG_INVALID_ID) sg_destroy_pipeline(shader->pipelines[s][b][d]);
            }
        }
        if (shader->programs[s].shader.id != SG_INVALID_ID) sg_destroy_shader(shader->programs[s].shader);
    }
}

/* -------------------------------------------------------------- loader ---- */

typedef struct {
    unsigned char *bytes;
    size_t size;
} sk_shader_file_t;

static void *prepare_shader(const char *path)
{
    FILE *f = path != NULL ? fopen(path, "rb") : NULL;
    sk_shader_file_t *file;
    long size;

    if (f == NULL) {
        log_error("sk_shader_create: can't open %s", path != NULL ? path : "(null)");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = calloc(1, sizeof(*file));
    if (file == NULL || size <= 0 || (file->bytes = malloc((size_t)size)) == NULL ||
        fread(file->bytes, 1, (size_t)size, f) != (size_t)size) {
        log_error("sk_shader_create: can't read %s", path);
        fclose(f);
        if (file != NULL) free(file->bytes);
        free(file);
        return NULL;
    }
    fclose(f);
    file->size = (size_t)size;
    return file;
}

static void discard_shader(void *data)
{
    sk_shader_file_t *file = (sk_shader_file_t *)data;
    if (file == NULL) return;
    free(file->bytes);
    free(file);
}

static sk_loader_step_t finish_shader(void *data, const char *path, sk_handle_t *resource)
{
    const sk_shader_file_t *file = (const sk_shader_file_t *)data;
    const char *slang = backend_slang();
    const char *error = NULL;
    program_desc_t *programs;
    sk_shader_t shader;
    uint16_t index = 0;
    bool ok;

    *resource = 0;
    if (slang == NULL) {
        log_error("sk_shader_create: %s: custom shaders aren't supported on this graphics backend", path);
        return SK_LOADER_FAILED;
    }
    programs = calloc(2, sizeof(program_desc_t));
    memset(&shader, 0, sizeof(shader));
    ok = programs != NULL && parse(file->bytes, file->size, slang, &shader, programs, &error);
    for (int i = 0; ok && i < 2; i++) {
        program_desc_t *p = &programs[i];
        sk_shader_program_t *program = &shader.programs[i];
        p->desc.vertex_func.source = p->sources[0];
        p->desc.vertex_func.entry = "main";
        p->desc.fragment_func.source = p->sources[1];
        p->desc.fragment_func.entry = "main";
        p->desc.label = i == 0 ? "sk-custom-static" : "sk-custom-skinned";
        memcpy(program->has_block, p->has_block, sizeof(program->has_block));
        for (int t = 0; t < SK_SHADER_MAX_TEXTURES; t++) {
            program->view_slot[t] = -1;
            program->sampler_slot[t] = -1;
        }
        for (int t = 0; t < shader.texture_count; t++) {
            for (int v = 0; v < SK_SHADER_MAX_TEXTURES; v++) {
                if (strcmp(p->view_names[v], shader.textures[t]) == 0) program->view_slot[t] = v;
            }
            for (int k = 0; k < p->pair_count; k++) {
                if (p->pair_view[k] == program->view_slot[t]) {
                    program->sampler_slot[t] = p->pair_sampler[k];
                    break;
                }
            }
        }
        program->shader = sg_make_shader(&p->desc);
        if (sg_query_shader_state(program->shader) != SG_RESOURCESTATE_VALID) {
            error = "the graphics backend refused it (see the error above)";
            ok = false;
        }
    }
    if (programs != NULL) free_programs(programs);
    free(programs);
    if (!ok) {
        log_error("sk_shader_create: %s: %s", path != NULL ? path : "(null)", error != NULL ? error : "out of memory");
        destroy_gpu(&shader);
        return SK_LOADER_FAILED;
    }
    if (path != NULL && path[0] != '\0') {
        snprintf(shader.path, sizeof(shader.path), "%s", path);
        shader.has_path = true;
    }
    shader.ref_count = 1;
    const sk_handle_t handle = sk_handle_pool_alloc(&sk_shader_pool);
    if (handle == 0) {
        log_error("shader: pool full (%u)", (unsigned)sk_shader_pool.max - 1u);
        destroy_gpu(&shader);
        return SK_LOADER_FAILED;
    }
    sk_handle_pool_resolve(&sk_shader_pool, handle, &index);
    sk_shaders[index] = shader;
    *resource = handle;
    return SK_LOADER_DONE;
}

static sk_handle_t find_shader(const char *path)
{
    if (path == NULL || path[0] == '\0') return 0;
    for (uint16_t i = 1; i < sk_shader_pool.capacity; i++) {
        if (sk_shader_pool.occupied[i] && sk_shaders[i].has_path && strcmp(sk_shaders[i].path, path) == 0) {
            sk_shaders[i].ref_count++;
            return sk_handle_pool_handle_from_index(&sk_shader_pool, i);
        }
    }
    return 0;
}

static const sk_loader_t sk_shader_loader = {
    .name = "shader",
    .prepare = prepare_shader,
    .finish = finish_shader,
    .discard = discard_shader,
    .find = find_shader,
    .release = sk_shader_release,
};

/* ---------------------------------------------------------- public API ---- */

SK_KEEP
sk_handle_t sk_shader_create(const char *path)
{
    return sk_loader_create(&sk_shader_loader, path);
}

void sk_shader_retain(sk_handle_t shader)
{
    sk_shader_t *shader_ptr = resolve(shader);
    if (shader_ptr != NULL) shader_ptr->ref_count++;
}

SK_KEEP
void sk_shader_release(sk_handle_t shader)
{
    sk_shader_t *shader_ptr = resolve(shader);
    if (shader_ptr == NULL) return;
    if (shader_ptr->ref_count > 0) shader_ptr->ref_count--;
    if (shader_ptr->ref_count == 0) {
        destroy_gpu(shader_ptr);
        memset(shader_ptr, 0, sizeof(*shader_ptr));
        sk_handle_pool_free(&sk_shader_pool, shader);
    }
}

void sk_shader_init(void)
{
    if (!sk_handle_pool_init(&sk_shader_pool, SK_HANDLE_KIND_SHADER, "shader", (void **)&sk_shaders,
                             sizeof(sk_shader_t), SHADERS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("shader: out of memory");
    }
    sk_asset_register_loader(".skshader", &sk_shader_loader);
    sk_shader_hooks = (sk_shader_hooks_t){
        .get = sk_shader_get,
        .retain = sk_shader_retain,
        .release = sk_shader_release,
        .find_param = sk_shader_find_param,
        .find_texture = sk_shader_find_texture,
    };
}

void sk_shader_deinit(void)
{
    for (uint16_t i = 1; i < sk_shader_pool.capacity; i++) {
        if (sk_shader_pool.occupied[i]) destroy_gpu(&sk_shaders[i]);
    }
    sk_handle_pool_destroy(&sk_shader_pool);
    sk_shader_hooks = (sk_shader_hooks_t){0};
}

/* An optional subsystem: part of the runtime when a program uses it (internal/sk_module.h).
 * Before materials, which hold shaders. */
static sk_module_t sk_shader_module = {.name = "shader", .order = 25, .init = sk_shader_init,
                                       .deinit = sk_shader_deinit};
SK_MODULE(sk_shader_module)
