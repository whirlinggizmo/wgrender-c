#ifndef SK_INTERNAL_MODEL_H
#define SK_INTERNAL_MODEL_H

void sk_model_init(void);
void sk_model_deinit(void);

/* Issue queued model draws into the current render pass. Called from
 * sk_render_end() (models use a custom sg pipeline, so they must run inside the
 * pass rather than recording into sokol_gl). */
void sk_model_flush(void);

#endif // SK_INTERNAL_MODEL_H
