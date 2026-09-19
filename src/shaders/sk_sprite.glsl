/* sk_sprite shaders — instanced sprite quads (docs/PLAN-sprites.md). Authored once in
 * annotated GLSL; sokol-shdc generates sk_sprite.glsl.h with GL core / WebGL2 / WebGPU
 * variants. Regen: `make shaders`.
 *
 * One quad (6 corners) drawn once per sprite. Each instance carries where the sprite
 * is and how big, its source rectangle, its tint, and how it faces:
 *   facing 0  camera-facing (spherical): the batch's camera right and up
 *   facing 1  upright, turning about Y (cylindrical): the batch's right, world up
 *   facing 2+ its own axes, in the instance (flat on the ground, or free)
 * The billboard axes come from the camera, the same for every sprite in a batch, so
 * they're uniforms; the CPU works them out exactly as picking does
 * (sk_sprite3d_facing_basis). Colors are sRGB values, like sokol_gl's: texture x tint.
 *
 * Alpha (inst_up.w): > 0 a mask cutoff (texels below it are discarded, the rest are
 * opaque), < 0 opaque, 0 as is (blended or added by the pipeline). */

@module sprite

@vs vs
layout(binding=0) uniform vs_params {
    mat4 view_proj;
    vec4 camera_right; /* xyz: facing 0's right */
    vec4 camera_up;    /* xyz: facing 0's up */
    vec4 upright;      /* xyz: facing 1's right (its up is world Y) */
};
in vec2 corner;        /* the quad's corner: x -0.5 left .. 0.5 right, y -0.5 bottom .. 0.5 top */
in vec4 inst_pos;      /* xyz where the pivot goes, w facing */
in vec4 inst_size;     /* xy world width and height (scale included), zw pivot (0..1, y down) */
in vec4 inst_uv;       /* u0, v0 (top-left), u1, v1 (bottom-right) */
in vec3 inst_right;    /* facing 2+: the quad's right axis */
in vec4 inst_up;       /* xyz facing 2+: the quad's up axis; w alpha (see above) */
in vec4 inst_color;    /* tint */
out vec2 uv;
out vec4 color;
out float alpha_mode;

void main() {
    vec3 right = inst_right;
    vec3 up = inst_up.xyz;
    if (inst_pos.w < 0.5) {
        right = camera_right.xyz;
        up = camera_up.xyz;
    } else if (inst_pos.w < 1.5) {
        right = upright.xyz;
        up = vec3(0.0, 1.0, 0.0);
    }
    /* the pivot sits on the position: the quad reaches (1 - pivot) of its size right of
       it and pivot.y of it up (the pivot runs down the texture) */
    vec2 local = vec2((corner.x + 0.5 - inst_size.z) * inst_size.x, (corner.y - 0.5 + inst_size.w) * inst_size.y);
    vec3 world = inst_pos.xyz + right * local.x + up * local.y;
    gl_Position = view_proj * vec4(world, 1.0);
    uv = vec2(mix(inst_uv.x, inst_uv.z, corner.x + 0.5), mix(inst_uv.y, inst_uv.w, 0.5 - corner.y));
    color = inst_color;
    alpha_mode = inst_up.w;
}
@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
in vec2 uv;
in vec4 color;
in float alpha_mode;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * color;
    if (alpha_mode > 0.0 && frag_color.a < alpha_mode) {
        discard;
    }
    if (alpha_mode != 0.0) {
        frag_color.a = 1.0;
    }
}
@end

@program quad vs fs
