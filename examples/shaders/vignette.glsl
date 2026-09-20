/* Vignette (examples/postprocess.c): the finished frame darkened toward the corners,
 * with a little warmth left in the middle. A screen effect — it includes wgr_screen, so
 * shaderpack builds one program that draws over the frame (make example-shaders). */
@fs fs
@include_block wgr_screen
layout(binding=2) uniform params {
    float strength;  /* how dark the corners go (0: none) */
    float radius;    /* where the darkening starts, as a fraction of the half-diagonal */
    vec4 tint;       /* multiplies the middle, linear rgba */
};

void main() {
    vec4 frame = wgr_screen_color();
    vec2 d = (wgr_screen_uv - 0.5) * 2.0;          /* -1..1 across the screen */
    float r = length(d) / 1.41421356;             /* 0 middle, 1 corner */
    float fade = 1.0 - smoothstep(radius, 1.0, r) * clamp(strength, 0.0, 1.0);
    wgr_output(frame.rgb * tint.rgb * fade, frame.a);
}
@end
