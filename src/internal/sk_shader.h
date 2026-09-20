#ifndef SK_INTERNAL_SHADER_H
#define SK_INTERNAL_SHADER_H

#include <stdbool.h>

#include "sk_shader.h"
#include "sk_types.h"
#include "sokol_gfx.h"

/* A custom material shader (.skshader, written by tools/shaderpack.py): the running
 * backend's programs, and its parameters and textures by name. A surface shader has
 * the model and sprite programs and draws through sk_model / sk_sprite_batch; a screen
 * shader (its fragment shader includes sk_screen) has one program and redraws the
 * finished frame through sk_effect. */

#define SK_SHADER_MAX_PARAMS 32
#define SK_SHADER_MAX_TEXTURES 8
#define SK_SHADER_NAME_MAX 32

typedef enum {
    SK_SHADER_PARAM_FLOAT = 0,
    SK_SHADER_PARAM_INT,
    SK_SHADER_PARAM_VEC2,
    SK_SHADER_PARAM_VEC3,
    SK_SHADER_PARAM_VEC4,
} sk_shader_param_type_t;

typedef enum {
    SK_SHADER_BLOCK_OBJECT = 0,    /* libsk's vertex block: matrices, time (and joints) */
    SK_SHADER_BLOCK_FRAME = 1,     /* libsk's fragment block: sk_frame in shaders/sk.glsl */
    SK_SHADER_BLOCK_FS_PARAMS = 2, /* the shader's fragment parameters */
    SK_SHADER_BLOCK_VS_PARAMS = 3, /* the vertex hook's parameters */
    SK_SHADER_BLOCK_SPRITE_BATCH = 4, /* sprites read from a texture: the batch's first sprite */
    SK_SHADER_BLOCK_COUNT,
} sk_shader_block_t;

typedef struct {
    char name[SK_SHADER_NAME_MAX];
    sk_shader_param_type_t type;
    sk_shader_block_t block; /* FS_PARAMS or VS_PARAMS */
    int offset;              /* bytes into the block (std140) */
} sk_shader_param_t;

typedef struct {
    sg_shader shader;
    bool has_block[SK_SHADER_BLOCK_COUNT]; /* blocks the program uses (unused ones are compiled out) */
    int view_slot[SK_SHADER_MAX_TEXTURES];    /* per texture: its view slot, -1 when unused */
    int sampler_slot[SK_SHADER_MAX_TEXTURES]; /* the sampler it's paired with, -1 none */
    int env_view_slot, env_sampler_slot;      /* libsk's environment cubemap (sk_env_tex), -1 unused */
    int brdf_view_slot, brdf_sampler_slot;    /* and its BRDF table (sk_brdf_tex) */
    int sprite_view_slot, sprite_sampler_slot; /* a sprite's texture (sk_sprite_tex) */
    int data_view_slot, data_sampler_slot;     /* sprites read from a texture (sk_sprite_data) */
    int joint_view_slot, joint_sampler_slot;   /* skinned models' joints (sk_joint_tex) */
    int screen_view_slot, screen_sampler_slot; /* a screen effect's frame (sk_screen_tex) */
} sk_shader_program_t;

enum {
    SK_SHADER_PROGRAM_STATIC,
    SK_SHADER_PROGRAM_SKINNED,
    SK_SHADER_PROGRAM_SPRITE,        /* per-instance attributes */
    SK_SHADER_PROGRAM_SPRITE_PULLED, /* sprites read from a texture (no base instance: WebGL2) */
    SK_SHADER_PROGRAM_SCREEN,        /* a screen effect: the only program a screen shader has */
    SK_SHADER_PROGRAM_COUNT,
};
#define SK_SHADER_SPRITE_PIPELINES 8 /* the sprite batch's pipeline kinds (src/sk_sprite_batch.c) */

typedef struct {
    sk_shader_program_t programs[SK_SHADER_PROGRAM_COUNT];
    sg_pipeline pipelines[2][2][2];  /* [skinned][blended][double_sided], made by sk_model on first use */
    sg_pipeline sprite_pipelines[SK_SHADER_SPRITE_PIPELINES]; /* made by the sprite batch on first use */
    sg_pipeline screen_pipeline; /* screen effects: made by sk_effect on first use */
    bool screen;                 /* a screen effect (sk_render_add_effect), not a surface shader */
    sk_shader_param_t params[SK_SHADER_MAX_PARAMS];
    int param_count;
    int block_size[SK_SHADER_BLOCK_COUNT]; /* FS_PARAMS / VS_PARAMS: bytes (std140, 16-byte multiple) */
    char textures[SK_SHADER_MAX_TEXTURES][SK_SHADER_NAME_MAX];
    int texture_count;
    char path[512];
    bool has_path;
    int ref_count;
} sk_shader_t;

/* Resolve without logging; NULL for 0 or a stale handle. The pointer is valid until
 * the next shader is created. */
sk_shader_t *sk_shader_get(sk_handle_t shader);
void sk_shader_init(void);
void sk_shader_deinit(void);
void sk_shader_retain(sk_handle_t shader);

/* Index of the parameter or texture called `name`, or -1. */
int sk_shader_find_param(const sk_shader_t *shader, const char *name);
int sk_shader_find_texture(const sk_shader_t *shader, const char *name);

/* Materials and models reach shaders through these, so a program that never loads a
 * shader doesn't link the shader module: it fills them in when it starts (it's linked
 * when sk_shader_create is), and they stay NULL otherwise, when no material can have
 * a shader. Defined in sk_material.c. */
/* The per-draw block every custom shader reads (sk_frame in shaders/sk.glsl), std140. */
typedef struct {
    float camera_time[4];   /* xyz camera position, w seconds */
    float tint[4];          /* linear rgba */
    float ambient_count[4]; /* rgb ambient, w number of lights */
    float output[4];        /* x alpha cutoff, y tone mapping, z exposure scale */
    float light_pos_range[8][4];
    float light_dir_type[8][4];
    float light_radiance[8][4];
    float light_spot[8][4];
    float env[4];
    float sh[9][4];
} sk_shader_frame_t;

typedef struct {
    sk_shader_t *(*get)(sk_handle_t shader);
    void (*retain)(sk_handle_t shader);
    void (*release)(sk_handle_t shader);
    int (*find_param)(const sk_shader_t *shader, const char *name);
    int (*find_texture)(const sk_shader_t *shader, const char *name);
    /* Textures for libsk's slots when there's nothing to show: white (2D), black
     * (cube), with a linear sampler. Made on first use. */
    void (*fallbacks)(sg_view *white, sg_view *black_cube, sg_sampler *linear);
} sk_shader_hooks_t;
extern sk_shader_hooks_t sk_shader_hooks;

#endif // SK_INTERNAL_SHADER_H
