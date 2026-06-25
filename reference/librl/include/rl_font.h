#ifndef RL_FONT_H
#define RL_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

extern const rl_handle_t RL_FONT_DEFAULT;


rl_handle_t rl_font_create(const char *filename, int fontSize) ;
void rl_font_destroy(rl_handle_t handle) ;
rl_handle_t rl_font_get_default() ;


#ifdef __cplusplus
}
#endif

#endif // RL_FONT_H
