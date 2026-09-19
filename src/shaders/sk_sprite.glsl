/* sk_sprite shaders — instanced sprite quads (docs/PLAN-sprites.md). Authored once in
 * annotated GLSL; sokol-shdc generates sk_sprite.glsl.h with GL core / WebGL2 / WebGPU
 * variants. Regen: `make shaders`.
 *
 * One quad (6 corners) drawn once per sprite. Each sprite carries where it is and how
 * big, its source rectangle, its tint, and how it faces:
 *   facing 0  camera-facing (spherical): the batch's camera right and up
 *   facing 1  upright, turning about Y (cylindrical): the batch's right, world up
 *   facing 2+ its own axes (flat on the ground, or free)
 * The billboard axes come from the camera, the same for every sprite in a batch, so
 * they're uniforms; the CPU works them out exactly as picking does
 * (sk_sprite3d_facing_basis). Colors are sRGB values, like sokol_gl's: texture x tint.
 *
 * Alpha: > 0 a mask cutoff (texels below it are discarded, the rest are opaque),
 * < 0 opaque, 0 as is (blended or added by the pipeline).
 *
 * Two ways to get a sprite's data:
 *   quad         per-instance vertex attributes (backends with base-instance draws)
 *   quad_pulled  fetched from a float texture by index: first + gl_InstanceID, 6 texels
 *                a sprite, 256 sprites a row (WebGL2 and GL before 4.2, which can't draw
 *                from a base instance: this way a batch doesn't rebind its instances)
 */

@module sprite

@block quad_common
layout(binding=0) uniform vs_params {
    mat4 view_proj;
    vec4 camera_right; /* xyz: facing 0's right */
    vec4 camera_up;    /* xyz: facing 0's up */
    vec4 upright;      /* xyz: facing 1's right (its up is world Y) */
};
out vec2 uv;
out vec4 color;
out float alpha_mode;

/* corner: x -0.5 left .. 0.5 right, y -0.5 bottom .. 0.5 top. pos: xyz where the pivot
   goes, w facing. size: xy world size (scale included), zw pivot (0..1, y down).
   source: u0, v0 (top-left), u1, v1. right, up: facing 2+ axes; up.w alpha. */
void place(vec2 corner, vec4 pos, vec4 size, vec4 source, vec3 right_axis, vec4 up_axis, vec4 tint) {
    vec3 right = right_axis;
    vec3 up = up_axis.xyz;
    if (pos.w < 0.5) {
        right = camera_right.xyz;
        up = camera_up.xyz;
    } else if (pos.w < 1.5) {
        right = upright.xyz;
        up = vec3(0.0, 1.0, 0.0);
    }
    /* the pivot sits on the position: the quad reaches (1 - pivot) of its size right of
       it and pivot.y of it up (the pivot runs down the texture) */
    vec2 local = vec2((corner.x + 0.5 - size.z) * size.x, (corner.y - 0.5 + size.w) * size.y);
    vec3 world = pos.xyz + right * local.x + up * local.y;
    gl_Position = view_proj * vec4(world, 1.0);
    uv = vec2(mix(source.x, source.z, corner.x + 0.5), mix(source.y, source.w, 0.5 - corner.y));
    color = tint;
    alpha_mode = up_axis.w;
}
@end

@vs vs
@include_block quad_common
in vec2 corner;
in vec4 inst_pos;
in vec4 inst_size;
in vec4 inst_uv;
in vec3 inst_right;
in vec4 inst_up;
in vec4 inst_color;

void main() {
    place(corner, inst_pos, inst_size, inst_uv, inst_right, inst_up, inst_color);
}
@end

@vs vs_pulled
@include_block quad_common
layout(binding=1) uniform vs_batch {
    vec4 batch; /* x: the batch's first sprite */
};
layout(binding=1) uniform texture2D sprite_data;
layout(binding=1) uniform sampler sprite_data_smp;
@image_sample_type sprite_data unfilterable_float
@sampler_type sprite_data_smp nonfiltering
in vec2 corner;

vec4 texel(int sprite, int k) {
    return texelFetch(sampler2D(sprite_data, sprite_data_smp), ivec2((sprite % 256) * 6 + k, sprite / 256), 0);
}

void main() {
    int sprite = int(batch.x) + gl_InstanceIndex;
    place(corner, texel(sprite, 0), texel(sprite, 1), texel(sprite, 2), texel(sprite, 3).xyz, texel(sprite, 4),
          texel(sprite, 5));
}
@end

@vs vs_particle
/* A particle from its birth record and its age (docs/PLAN-sprites.md, step 4): the CPU
   writes a particle once, when it's born; where it is, how big, what color and how
   turned all follow from how long ago that was. One draw per emitter. */
layout(binding=0) uniform particle_params {
    mat4 view_proj;
    vec4 axis_x;      /* xyz: the quad's right before spin (3D: the camera's right; 2D: +x) */
    vec4 axis_y;      /* xyz: its up (3D: the camera's up; 2D: up the screen) */
    vec4 gravity_now; /* xyz gravity, w the emitter's time now (seconds) */
    vec4 size_mode;   /* x size at birth, y size at death, z alpha (as sprites' up.w) */
    vec4 color_start;
    vec4 color_end;
    vec4 source;      /* the texture region: u0, v0, u1, v1 */
    vec4 dynamics;    /* x drag (per second), y stretch (seconds of motion; 0 none) */
};
in vec2 corner;
in vec4 born;   /* xyz where, w when */
in vec4 motion; /* xyz velocity, w life (seconds) */
in vec4 shape;  /* x size scale, y spin (radians / s), z angle at birth */
out vec2 uv;
out vec4 color;
out float alpha_mode;

void main() {
    float age = gravity_now.w - born.w;
    float t = age / motion.w;
    if (age < 0.0 || t >= 1.0) { /* not born yet, or dead: nothing to draw */
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        uv = vec2(0.0);
        color = vec4(0.0);
        alpha_mode = 0.0;
        return;
    }
    /* gravity, and drag: slowing in proportion to speed, towards gravity / drag */
    vec3 pos;
    vec3 vel;
    float drag = dynamics.x;
    if (drag > 0.0001) {
        vec3 terminal = gravity_now.xyz / drag;
        float decay = exp(-drag * age);
        pos = born.xyz + terminal * age + (motion.xyz - terminal) * ((1.0 - decay) / drag);
        vel = terminal + (motion.xyz - terminal) * decay;
    } else {
        pos = born.xyz + motion.xyz * age + 0.5 * gravity_now.xyz * age * age;
        vel = motion.xyz + gravity_now.xyz * age;
    }
    float size = mix(size_mode.x, size_mode.y, t) * shape.x;
    float along = size;
    vec3 right;
    vec3 up;
    /* stretched: the quad's up follows the velocity across the screen, as long as the
       distance moved in `stretch` seconds (plus its size), trailing behind */
    vec2 screen_vel = vec2(dot(vel, axis_x.xyz), dot(vel, axis_y.xyz));
    float speed = length(screen_vel);
    if (dynamics.y > 0.0 && speed > 0.00001) {
        vec2 d = screen_vel / speed;
        up = axis_x.xyz * d.x + axis_y.xyz * d.y;
        right = axis_x.xyz * d.y - axis_y.xyz * d.x;
        float extra = speed * dynamics.y;
        along = size + extra;
        pos -= up * (extra * 0.5);
    } else {
        float angle = shape.z + shape.y * age; /* positive turns clockwise on screen */
        float c = cos(angle);
        float s = sin(angle);
        right = axis_x.xyz * c - axis_y.xyz * s;
        up = axis_x.xyz * s + axis_y.xyz * c;
    }
    gl_Position = view_proj * vec4(pos + right * (corner.x * size) + up * (corner.y * along), 1.0);
    uv = vec2(mix(source.x, source.z, corner.x + 0.5), mix(source.y, source.w, 0.5 - corner.y));
    color = mix(color_start, color_end, t);
    alpha_mode = size_mode.z;
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
@program quad_pulled vs_pulled fs
@program particle vs_particle fs
