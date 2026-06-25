#ifndef RL_PICK_H
#define RL_PICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "rl_types.h"

typedef struct
{
    bool hit;
    rl_handle_t handle;
    float distance;
    vec3_t point;
    vec3_t normal;
} rl_pick_result_t;

typedef struct
{
    int broadphase_tests;
    int broadphase_rejects;
    int narrowphase_tests;
    int narrowphase_hits;
} rl_pick_stats_t;

rl_pick_result_t rl_pick_model(rl_handle_t camera,
                               rl_handle_t model,
                               float mouse_x,
                               float mouse_y);

bool rl_pick_model_to_scratch(rl_handle_t camera,
                              rl_handle_t model,
                              float mouse_x,
                              float mouse_y);

rl_pick_result_t rl_pick_sprite3d(rl_handle_t camera,
                                  rl_handle_t sprite3d,
                                  float mouse_x,
                                  float mouse_y);

bool rl_pick_sprite3d_to_scratch(rl_handle_t camera,
                                 rl_handle_t sprite3d,
                                 float mouse_x,
                                 float mouse_y);

rl_pick_result_t rl_pick_shape(rl_handle_t camera,
                               rl_handle_t shape,
                               float mouse_x,
                               float mouse_y);

bool rl_pick_shape_to_scratch(rl_handle_t camera,
                              rl_handle_t shape,
                              float mouse_x,
                              float mouse_y);

rl_pick_result_t rl_pick_text3d(rl_handle_t camera,
                                rl_handle_t text3d,
                                float mouse_x,
                                float mouse_y);

bool rl_pick_text3d_to_scratch(rl_handle_t camera,
                               rl_handle_t text3d,
                               float mouse_x,
                               float mouse_y);

void rl_pick_reset_stats(void);
int rl_pick_get_broadphase_tests(void);
int rl_pick_get_broadphase_rejects(void);
int rl_pick_get_narrowphase_tests(void);
int rl_pick_get_narrowphase_hits(void);

#ifdef __cplusplus
}
#endif

#endif // RL_PICK_H
