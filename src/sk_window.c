#include "sk_window.h"

#include "internal/exports.h"

#include "sokol_app.h"

SK_KEEP
void sk_window_set_title(const char *title)
{
    sapp_set_window_title(title);
}

SK_KEEP
int sk_window_close_requested(void)
{
    /* sokol_app drives the loop and tears down on quit, so there is no
     * poll-style "should close" query. Reported via the cleanup callback. */
    return 0;
}

vec2_t sk_window_get_screen_size(void)
{
    return (vec2_t){(float)sapp_width(), (float)sapp_height()};
}

vec2_t sk_window_get_position(void)
{
    /* sokol_app does not expose window position; report origin. */
    return (vec2_t){0.0f, 0.0f};
}
