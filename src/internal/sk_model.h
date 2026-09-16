#ifndef SK_INTERNAL_MODEL_H
#define SK_INTERNAL_MODEL_H

void sk_model_init(void);
void sk_model_deinit(void);

/* Draw queued model items [first, first + count) into the current render pass,
 * in queue order. Called by sk_render_end() while replaying the frame's command
 * list (models use a custom sg pipeline, so they must run inside the pass). */
void sk_model_draw_items(int first, int count);

/* Clear the model draw queue after the frame has been drawn. */
void sk_model_end_frame(void);

#endif // SK_INTERNAL_MODEL_H
