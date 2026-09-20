#ifndef SK_INTERNAL_EFFECT_H
#define SK_INTERNAL_EFFECT_H

/* Screen effects (sk_render_add_effect, src/sk_effect.c): an optional subsystem, part
 * of the runtime when a program adds one. */

void sk_effect_init(void);
void sk_effect_deinit(void);

#endif // SK_INTERNAL_EFFECT_H
