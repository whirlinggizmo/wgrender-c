/* wgr_sprite shaders — instanced sprite quads (docs/PLAN-sprites.md). Authored once in
 * annotated GLSL; sokol-shdc generates wgr_sprite.glsl.h with GL core / WebGL2 / WebGPU
 * variants. Regen: `make shaders`.
 *
 * One quad (6 corners) drawn once per sprite. Each sprite carries where it is and how
 * big, its source rectangle, its tint, and how it faces:
 *   facing 0  camera-facing (spherical): the batch's camera right and up
 *   facing 1  upright, turning about Y (cylindrical): the batch's right, world up
 *   facing 2+ its own axes (flat on the ground, or free)
 * The billboard axes come from the camera, the same for every sprite in a batch, so
 * they're uniforms; the CPU works them out exactly as picking does
 * (wgri_sprite3d_facing_basis). Colors are sRGB values, like sokol_gl's: texture x tint.
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
    vec4 counts;      /* x size keys, y color keys, z alpha (as sprites' up.w), w palette colors */
    vec4 source;      /* the texture region: u0, v0, u1, v1 */
    vec4 dynamics;    /* x drag (per second), y stretch (seconds of motion; 0 none) */
    vec4 frames;      /* flipbook: x columns, y rows, z frames (< 2: none), w per second (0: over life) */
    vec4 size_times[2];  /* up to 8 keys over life (0..1), in order */
    vec4 size_values[2];
    vec4 color_times[2];
    vec4 color_values[8];
    vec4 palette[8];     /* each particle's tint, picked at birth */
};
in vec2 corner;
in vec4 born;   /* xyz where, w when */
in vec4 motion; /* xyz velocity, w life (seconds) */
in vec4 shape;  /* x size scale, y spin (radians / s), z angle at birth, w a random 0..1 */
out vec2 uv;
out vec4 color;
out float alpha_mode;

/* key i of a curve packed 4 to a vec4 */
float size_time(int i) { vec4 q = size_times[i / 4]; return q[i % 4]; }
float size_value(int i) { vec4 q = size_values[i / 4]; return q[i % 4]; }
float color_time(int i) { vec4 q = color_times[i / 4]; return q[i % 4]; }

/* A curve at t: its first key's value before it, its last's after, and between two keys
   the line between them. */
float size_at(float t) {
    int n = int(counts.x);
    float value = size_value(0);
    float prev_t = size_time(0);
    for (int i = 1; i < 8; i++) {
        if (i >= n || t <= prev_t) break;
        float key_t = size_time(i);
        float key_v = size_value(i);
        value = t >= key_t ? key_v : mix(value, key_v, (t - prev_t) / max(key_t - prev_t, 0.000001));
        prev_t = key_t;
    }
    return value;
}

vec4 color_at(float t) {
    int n = int(counts.y);
    vec4 value = color_values[0];
    float prev_t = color_time(0);
    for (int i = 1; i < 8; i++) {
        if (i >= n || t <= prev_t) break;
        float key_t = color_time(i);
        value = t >= key_t ? color_values[i] : mix(value, color_values[i], (t - prev_t) / max(key_t - prev_t, 0.000001));
        prev_t = key_t;
    }
    return value;
}

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
    float size = size_at(t) * shape.x;
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
    /* the flipbook's frame: played once over the life, or looped from a random frame */
    vec4 cell = source;
    if (frames.z > 1.5) {
        float frame = frames.w > 0.0 ? mod(floor(age * frames.w + fract(shape.w * 7.31) * frames.z), frames.z)
                                     : min(floor(t * frames.z), frames.z - 1.0);
        vec2 step = (source.zw - source.xy) / frames.xy;
        cell.xy = source.xy + step * vec2(mod(frame, frames.x), floor(frame / frames.x));
        cell.zw = cell.xy + step;
    }
    uv = vec2(mix(cell.x, cell.z, corner.x + 0.5), mix(cell.y, cell.w, 0.5 - corner.y));
    color = color_at(t);
    int colors = int(counts.w);
    if (colors > 0) {
        color *= palette[min(int(shape.w * float(colors)), colors - 1)];
    }
    alpha_mode = counts.z;
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

/* Lit sprites (materials phase 3b): the same shading as models, from the sprite's
 * texture (its base color), the material's other maps and the scene's lights, which
 * the batch chooses once (src/wgr_sprite_batch.c). The surface faces where the quad
 * does. wgr_pbr.glsl holds the shading; only the vertex shaders differ from a model's. */
@include wgr_pbr.glsl

@block quad_lit_common
@include_block wgr_pbr_color
layout(binding=0) uniform vs_params {
    mat4 view_proj;
    vec4 camera_right;
    vec4 camera_up;
    vec4 upright;
};
/* in the order wgr_pbr.glsl's fragment shader reads them */
out vec3 v_normal;
out vec4 v_tangent;
out vec2 v_uv0;
out vec2 v_uv1;
out vec4 v_color;
out vec3 v_world_pos;
out float v_alpha_mode;

void place_lit(vec2 corner, vec4 pos, vec4 size, vec4 source, vec3 right_axis, vec4 up_axis, vec4 tint) {
    vec3 right = right_axis;
    vec3 up = up_axis.xyz;
    if (pos.w < 0.5) {
        right = camera_right.xyz;
        up = camera_up.xyz;
    } else if (pos.w < 1.5) {
        right = upright.xyz;
        up = vec3(0.0, 1.0, 0.0);
    }
    vec2 local = vec2((corner.x + 0.5 - size.z) * size.x, (corner.y - 0.5 + size.w) * size.y);
    vec3 world = pos.xyz + right * local.x + up * local.y;
    gl_Position = view_proj * vec4(world, 1.0);
    v_world_pos = world;
    v_normal = cross(right, up); /* the quad faces the way it's seen */
    v_tangent = vec4(right, 1.0);
    v_uv0 = vec2(mix(source.x, source.z, corner.x + 0.5), mix(source.y, source.w, 0.5 - corner.y));
    v_uv1 = v_uv0;
    v_color = vec4(srgb_to_linear(tint.rgb), tint.a); /* its tint, like a model's vertex color */
    v_alpha_mode = up_axis.w;
}
@end

@vs vs_lit
@include_block quad_lit_common
in vec2 corner;
in vec4 inst_pos;
in vec4 inst_size;
in vec4 inst_uv;
in vec3 inst_right;
in vec4 inst_up;
in vec4 inst_color;
void main() {
    place_lit(corner, inst_pos, inst_size, inst_uv, inst_right, inst_up, inst_color);
}
@end

@vs vs_lit_pulled
@include_block quad_lit_common
layout(binding=4) uniform vs_batch_lit {
    vec4 batch_lit; /* x: the batch's first sprite */
};
/* slots 0-6 are the material's textures and the environment's (wgr_pbr.glsl) */
layout(binding=7) uniform texture2D sprite_data_lit;
layout(binding=7) uniform sampler sprite_data_lit_smp;
@image_sample_type sprite_data_lit unfilterable_float
@sampler_type sprite_data_lit_smp nonfiltering
in vec2 corner;

vec4 texel_lit(int sprite, int k) {
    return texelFetch(sampler2D(sprite_data_lit, sprite_data_lit_smp), ivec2((sprite % 256) * 6 + k, sprite / 256), 0);
}

void main() {
    int sprite = int(batch_lit.x) + gl_InstanceIndex;
    place_lit(corner, texel_lit(sprite, 0), texel_lit(sprite, 1), texel_lit(sprite, 2), texel_lit(sprite, 3).xyz,
              texel_lit(sprite, 4), texel_lit(sprite, 5));
}
@end

@fs fs_lit
@include_block wgr_pbr_surface
@include_block wgr_pbr_main
@end

@program quad vs fs
@program quad_pulled vs_pulled fs
@program particle vs_particle fs
@program quad_lit vs_lit fs_lit
@program quad_lit_pulled vs_lit_pulled fs_lit
