#ifndef SK_WINDOW_H
#define SK_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Window flags. Only a subset is honored by the sokol_app backend; the rest are
 * accepted for source compatibility and ignored where the backend has no
 * equivalent. */
#define SK_WINDOW_FLAG_FULLSCREEN_MODE 0x00000002u
#define SK_WINDOW_FLAG_WINDOW_RESIZABLE 0x00000004u
#define SK_WINDOW_FLAG_WINDOW_UNDECORATED 0x00000008u
#define SK_WINDOW_FLAG_WINDOW_TRANSPARENT 0x00000010u
#define SK_WINDOW_FLAG_MSAA_4X_HINT 0x00000020u
#define SK_WINDOW_FLAG_VSYNC_OFF 0x00000040u /* unlock from vsync (desktop; the web is always vsynced) */
#define SK_WINDOW_FLAG_WINDOW_HIDDEN 0x00000080u
#define SK_WINDOW_FLAG_WINDOW_ALWAYS_RUN 0x00000100u
#define SK_WINDOW_FLAG_WINDOW_HIGHDPI 0x00002000u

/*
 * Window lifecycle is owned by the core runtime:
 * - sk_init_values(...) configures the window
 * - sk_run() opens it (sokol_app owns the message loop)
 * - sk_deinit()/cleanup closes it
 */

void sk_window_set_title(const char *title);
int sk_window_close_requested(void);
vec2_t sk_window_get_screen_size(void);
vec2_t sk_window_get_position(void);

#ifdef __cplusplus
}
#endif

#endif // SK_WINDOW_H
