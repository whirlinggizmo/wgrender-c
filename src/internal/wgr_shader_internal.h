#ifndef WGRI_INTERNAL_SHADER_H
#define WGRI_INTERNAL_SHADER_H

#include <stdbool.h>

#include "wgr_shader.h"
#include "wgr_types.h"
#include "sokol_gfx.h"

/* A custom material shader (.wgrshader, written by tools/shaderpack.py): the running
 * backend's programs, and its parameters and textures by name. A surface shader has
 * the model and sprite programs and draws through wgr_model / wgr_sprite_batch; a screen
 * shader (its fragment shader includes wgr_screen) has one program and redraws the
 * finished frame through wgr_effect. */

#define WGRI_SHADER_MAX_PARAMS 32
#define WGRI_SHADER_MAX_TEXTURES 8
#define WGRI_SHADER_NAME_MAX 32

typedef enum {
    WGRI_SHADER_PARAM_FLOAT = 0,
    WGRI_SHADER_PARAM_INT,
    WGRI_SHADER_PARAM_VEC2,
    WGRI_SHADER_PARAM_VEC3,
    WGRI_SHADER_PARAM_VEC4,
} wgri_shader_param_type_t;

typedef enum {
    WGRI_SHADER_BLOCK_OBJECT = 0,    /* libwgrender's vertex block: matrices, time (and joints) */
    WGRI_SHADER_BLOCK_FRAME = 1,     /* libwgrender's fragment block: wgr_frame in shaders/wgr.glsl */
    WGRI_SHADER_BLOCK_FS_PARAMS = 2, /* the shader's fragment parameters */
    WGRI_SHADER_BLOCK_VS_PARAMS = 3, /* the vertex hook's parameters */
    WGRI_SHADER_BLOCK_SPRITE_BATCH = 4, /* sprites read from a texture: the batch's first sprite */
    WGRI_SHADER_BLOCK_COUNT,
} wgri_shader_block_t;

typedef struct {
    char name[WGRI_SHADER_NAME_MAX];
    wgri_shader_param_type_t type;
    wgri_shader_block_t block; /* FS_PARAMS or VS_PARAMS */
    int offset;              /* bytes into the block (std140) */
} wgri_shader_param_t;

typedef struct {
    sg_shader shader;
    bool has_block[WGRI_SHADER_BLOCK_COUNT]; /* blocks the program uses (unused ones are compiled out) */
    int view_slot[WGRI_SHADER_MAX_TEXTURES];    /* per texture: its view slot, -1 when unused */
    int sampler_slot[WGRI_SHADER_MAX_TEXTURES]; /* the sampler it's paired with, -1 none */
    int env_view_slot, env_sampler_slot;      /* libwgrender's environment cubemap (wgr_env_tex), -1 unused */
    int brdf_view_slot, brdf_sampler_slot;    /* and its BRDF table (wgr_brdf_tex) */
    int sprite_view_slot, sprite_sampler_slot; /* a sprite's texture (wgr_sprite_tex) */
    int data_view_slot, data_sampler_slot;     /* sprites read from a texture (wgr_sprite_data) */
    int joint_view_slot, joint_sampler_slot;   /* skinned models' joints (wgr_joint_tex) */
    int instance_view_slot, instance_sampler_slot; /* the frame's placements (wgr_instance_tex) */
    int screen_view_slot, screen_sampler_slot; /* a screen effect's frame (wgr_screen_tex) */
    int shadow_view_slot, shadow_sampler_slot; /* the casting light's map (wgr_shadow_tex) */
} wgri_shader_program_t;

enum {
    WGRI_SHADER_PROGRAM_STATIC,
    WGRI_SHADER_PROGRAM_SKINNED,
    WGRI_SHADER_PROGRAM_SPRITE,        /* per-instance attributes */
    WGRI_SHADER_PROGRAM_SPRITE_PULLED, /* sprites read from a texture (no base instance: WebGL2) */
    WGRI_SHADER_PROGRAM_SCREEN,        /* a screen effect: the only program a screen shader has */
    WGRI_SHADER_PROGRAM_COUNT,
};
#define WGRI_SHADER_SPRITE_PIPELINES 8 /* the sprite batch's pipeline kinds (src/wgr_sprite_batch.c) */

typedef struct {
    wgri_shader_program_t programs[WGRI_SHADER_PROGRAM_COUNT];
    sg_pipeline pipelines[2][2][2];  /* [skinned][blended][double_sided], made by wgr_model on first use */
    sg_pipeline sprite_pipelines[WGRI_SHADER_SPRITE_PIPELINES]; /* made by the sprite batch on first use */
    sg_pipeline screen_pipeline; /* screen effects: made by wgr_effect on first use */
    bool screen;                 /* a screen effect (wgr_render_add_effect), not a surface shader */
    wgri_shader_param_t params[WGRI_SHADER_MAX_PARAMS];
    int param_count;
    int block_size[WGRI_SHADER_BLOCK_COUNT]; /* FS_PARAMS / VS_PARAMS: bytes (std140, 16-byte multiple) */
    char textures[WGRI_SHADER_MAX_TEXTURES][WGRI_SHADER_NAME_MAX];
    int texture_count;
    char path[512];
    bool has_path;
    int ref_count;
} wgri_shader_t;

/* Resolve without logging; NULL for 0 or a stale handle. The pointer is valid until
 * the next shader is created. */
wgri_shader_t *wgri_shader_get(wgr_handle_t shader);
void wgri_shader_init(void);
void wgri_shader_deinit(void);
void wgri_shader_retain(wgr_handle_t shader);

/* Index of the parameter or texture called `name`, or -1. */
int wgri_shader_find_param(const wgri_shader_t *shader, const char *name);
int wgri_shader_find_texture(const wgri_shader_t *shader, const char *name);

/* Materials and models reach shaders through these, so a program that never loads a
 * shader doesn't link the shader module: it fills them in when it starts (it's linked
 * when wgr_shader_create is), and they stay NULL otherwise, when no material can have
 * a shader. Defined in wgr_material.c. */
/* The per-draw block every custom shader reads (wgr_frame in shaders/wgr.glsl), std140. */
typedef struct {
    float camera_time[4];   /* xyz camera position, w seconds */
    float ambient_count[4]; /* rgb ambient, w number of lights */
    float output[4];        /* x alpha cutoff, y tone mapping, z exposure scale */
    float light_pos_range[8][4];
    float light_dir_type[8][4];
    float light_radiance[8][4];
    float light_spot[8][4];
    float env[4];
    float sh[9][4];
    /* shadows: up to four casting lights, a layer of the map each (wgr_shadow.h) */
    float shadow_mat[4][16];
    float shadow_params[4][4];
    float shadow_tint[4][4];
    float shadow_extra[4][4];
    float shadow_map[4];
} wgri_shader_frame_t;

typedef struct {
    wgri_shader_t *(*get)(wgr_handle_t shader);
    void (*retain)(wgr_handle_t shader);
    void (*release)(wgr_handle_t shader);
    int (*find_param)(const wgri_shader_t *shader, const char *name);
    int (*find_texture)(const wgri_shader_t *shader, const char *name);
    /* Textures for libwgrender's slots when there's nothing to show: white (2D), black
     * (cube), with a linear sampler. Made on first use. */
    void (*fallbacks)(sg_view *white, sg_view *black_cube, sg_sampler *linear);
} wgri_shader_hooks_t;
extern wgri_shader_hooks_t wgri_shader_hooks;

#endif // WGRI_INTERNAL_SHADER_H
