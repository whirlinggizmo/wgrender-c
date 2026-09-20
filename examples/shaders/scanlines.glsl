/* CRT scanlines (examples/postprocess.c): dark lines across the frame, a slight color
 * shift left and right, and a slow flicker — a second screen effect, to show a chain.
 * Built with tools/shaderpack.py (make example-shaders). */
@fs fs
@include_block wgr_screen
layout(binding=2) uniform params {
    float lines;    /* dark lines down the screen */
    float darkness; /* how dark they go (0..1) */
    float offset;   /* color shift, in pixels */
    float flicker;  /* brightness wobble over time (0..1) */
};

void main() {
    vec2 texel = wgr_screen_texel();
    /* the red and blue channels come from a little left and right of this pixel */
    vec3 color = vec3(wgr_screen_color_at(wgr_screen_uv - vec2(offset * texel.x, 0.0)).r,
                      wgr_screen_color().g,
                      wgr_screen_color_at(wgr_screen_uv + vec2(offset * texel.x, 0.0)).b);
    float line = 0.5 + 0.5 * cos(wgr_screen_uv.y * max(lines, 1.0) * 6.2831853);
    color *= 1.0 - clamp(darkness, 0.0, 1.0) * line;
    color *= 1.0 + clamp(flicker, 0.0, 1.0) * 0.06 * sin(wgr_time() * 11.0);
    wgr_output(color, 1.0);
}
@end
