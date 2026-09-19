/* libsk custom shaders: what libsk gives a material's shader (docs/PLAN-materials.md,
 * "Custom shaders"). tools/shaderpack.py puts this file in front of yours, adds libsk's
 * vertex shaders (static and skinned models, and sprites) and compiles the result for
 * every backend into a .skshader file (sk_shader_create). One shader draws models and
 * sprites (sk_sprite3d_set_material, sk_sprite2d_set_material) alike.
 *
 * Your file has a fragment shader named `fs`, and may have a vertex hook:
 *
 *     @block vertex                           (optional)
 *     void sk_vertex(inout vec3 position, inout vec3 normal) { ... }   object space,
 *     @end                                    before skinning; sk_time() is available.
 *                                             Models only: sprites don't run it.
 *
 *     @fs fs
 *     @include_block sk_surface
 *     layout(binding=2) uniform params { vec4 tint; float speed; };  your parameters
 *     layout(binding=0) uniform texture2D noise_tex;                  your textures
 *     layout(binding=0) uniform sampler noise_smp;
 *     void main() {
 *         vec4 n = texture(sampler2D(noise_tex, noise_smp), sk_uv0);
 *         sk_output(tint.rgb * n.r, 1.0);
 *     }
 *     @end
 *
 * Textures and samplers 8 to 11 are libsk's (the environment, the sprite's texture,
 * sprite data), as are uniform blocks 0, 1 and 4.
 * Parameters: the members of uniform block binding 2 in the fragment shader and
 * binding 3 in the vertex hook, set by name with sk_material_set_float / _vec2 /
 * _vec3 / _vec4 / _int / _color (float, vec2, vec3, vec4, int; not arrays or
 * matrices). Names must differ between the two blocks. Colors given as sk_color_t
 * are converted to linear, like built-in materials. Textures: each texture2D (up to
 * 8, fragment shader, bindings 0-7) is set by its name with sk_material_set_texture,
 * and sampled with the sampler it's paired with in texture(sampler2D(...)), set up
 * by sk_material_set_texture_sampling. A texture that isn't set is white.
 *
 * Fragment inputs (sk_surface), world space (a 2D sprite's: screen pixels):
 *   sk_world_pos   vec3   the surface point
 *   sk_normal      vec3   interpolated vertex normal (not normalized; facing out of
 *                         the front face; a sprite's faces its front)
 *   sk_tangent     vec4   xyz tangent, w bitangent sign (glTF; a sprite's: its right)
 *   sk_uv0, sk_uv1 vec2   texture coordinate sets 0 and 1. A sprite's: its texture
 *                         region (uv0), and 0..1 across the quad whatever the region (uv1)
 *   sk_color       vec4   vertex color, linear (white when the mesh has none); a
 *                         sprite's: its tint, linear
 * and functions:
 *   sk_time()             seconds since the program started
 *   sk_camera_position()  world space
 *   sk_ambient()          the scene's ambient light, linear rgb (0 outside a scene)
 *   sk_light_count()      lights reaching this model (0..8)
 *   sk_light(i, pos, out to_light)   light i's radiance arriving at world position
 *                         `pos` (linear rgb, attenuated), and the unit direction from
 *                         `pos` toward the light
 *   sk_environment_intensity()   the scene's environment lighting strength (0: none;
 *                         the functions below then return black)
 *   sk_environment_diffuse(n)    light from the environment arriving around normal n
 *                         (irradiance / pi, linear rgb): times the diffuse color
 *   sk_environment_specular(n, v, roughness)   the environment reflected toward the
 *                         viewer (v: unit vector from the surface to the camera), blurred
 *                         for perceptual roughness 0..1 (linear rgb)
 *   sk_environment_brdf(n_dot_v, roughness)   split-sum scale (x) and bias (y): specular
 *                         reflection = sk_environment_specular(...) * (f0 * x + y)
 *   Built-in materials use exactly these (docs/PLAN-environment.md).
 *   sk_sprite_color()     a sprite's texture at its region times its tint, linear rgba
 *                         (on a model: its vertex color). The texture itself is
 *                         sk_sprite_tex / sk_sprite_smp, to sample it yourself (an
 *                         outline reads the texels around), converting its rgb to linear
 *   sk_srgb_to_linear(c), sk_linear_to_srgb(c)
 *   sk_output(color, alpha)   write the pixel: `color` is linear rgb. Applies the
 *                         model's tint, the material's (or sprite's) alpha mode, and
 *                         the scene's exposure and tone mapping (not on sprites), then
 *                         encodes sRGB. Call it once, at the end.
 * Lighting is linear and, like built-in materials, the framebuffer holds sRGB.
 * Names starting with sk_ are libsk's. */

@block sk_color
vec3 sk_srgb_to_linear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((max(c, vec3(0.04045)) + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 sk_linear_to_srgb(vec3 c) {
    c = clamp(c, vec3(0.0), vec3(1.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0031308)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}
@end

/* ----------------------------------------------------------------- vertex ---- */

@block sk_vertex_default
void sk_vertex(inout vec3 position, inout vec3 normal) {
}
@end

@block sk_vs_outputs
layout(location=0) out vec3 sk_world_pos;
layout(location=1) out vec3 sk_normal;
layout(location=2) out vec4 sk_tangent;
layout(location=3) out vec2 sk_uv0;
layout(location=4) out vec2 sk_uv1;
layout(location=5) out vec4 sk_color;
layout(location=6) out float sk_sprite_alpha; /* sprites: their alpha mode (0 for models) */
/* locations match libsk's vertex buffers */
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord0;
layout(location=3) in vec2 texcoord1;
layout(location=4) in vec4 tangent;
layout(location=5) in vec4 color0;
@end

@block sk_vs_static_uniforms
layout(binding=0) uniform sk_object {
    mat4 sk_mvp;
    mat4 sk_model;
    mat4 sk_normal_mat; /* inverse transpose of sk_model */
    vec4 sk_object_time; /* x seconds */
};
float sk_time() { return sk_object_time.x; }
@end

@block sk_vs_static_main
void main() {
    vec3 p = position;
    vec3 n = normal;
    sk_vertex(p, n);
    gl_Position = sk_mvp * vec4(p, 1.0);
    sk_world_pos = (sk_model * vec4(p, 1.0)).xyz;
    sk_normal = mat3(sk_normal_mat) * n;
    sk_tangent = vec4(mat3(sk_model) * tangent.xyz, tangent.w);
    sk_uv0 = texcoord0;
    sk_uv1 = texcoord1;
    sk_color = color0;
    sk_sprite_alpha = 0.0;
}
@end

@block sk_vs_skinned_uniforms
layout(binding=0) uniform sk_skinned_object {
    mat4 sk_mvp;
    mat4 sk_model;
    mat4 sk_normal_mat;
    vec4 sk_object_time;
    mat4 sk_joints[128];
};
layout(location=6) in vec4 joints;
layout(location=7) in vec4 weights;
float sk_time() { return sk_object_time.x; }
@end

@block sk_vs_skinned_main
void main() {
    vec3 p = position;
    vec3 n = normal;
    sk_vertex(p, n);
    mat4 skin = weights.x * sk_joints[int(joints.x)]
              + weights.y * sk_joints[int(joints.y)]
              + weights.z * sk_joints[int(joints.z)]
              + weights.w * sk_joints[int(joints.w)];
    vec4 sp = skin * vec4(p, 1.0);
    gl_Position = sk_mvp * sp;
    sk_world_pos = (sk_model * sp).xyz;
    sk_normal = mat3(sk_normal_mat) * (mat3(skin) * n);
    sk_tangent = vec4(mat3(sk_model) * (mat3(skin) * tangent.xyz), tangent.w);
    sk_uv0 = texcoord0;
    sk_uv1 = texcoord1;
    sk_color = color0;
    sk_sprite_alpha = 0.0;
}
@end

/* Sprites (src/sk_sprite_batch.c): one quad per sprite, placed as libsk's own sprite
 * shader places it (src/shaders/sk_sprite.glsl), from per-instance attributes, or from
 * a texture of sprite data where the backend can't draw from a base instance (WebGL2). */
@block sk_vs_sprite_common
@include_block sk_color
layout(binding=0) uniform sk_sprite_view {
    mat4 sk_view_proj;
    vec4 sk_camera_right; /* xyz: camera-facing sprites' right */
    vec4 sk_camera_up;
    vec4 sk_upright;      /* xyz: upright sprites' right (their up is world Y) */
    vec4 sk_sprite_time;  /* x seconds */
};
layout(location=0) out vec3 sk_world_pos;
layout(location=1) out vec3 sk_normal;
layout(location=2) out vec4 sk_tangent;
layout(location=3) out vec2 sk_uv0;
layout(location=4) out vec2 sk_uv1;
layout(location=5) out vec4 sk_color;
layout(location=6) out float sk_sprite_alpha;
float sk_time() { return sk_sprite_time.x; }

/* corner: -0.5..0.5 each way (y up). pos: where the pivot goes, w facing. size: world
   size, then pivot (0..1, y down). source: its texture region. right, up: its own axes
   (facing 2+); up.w its alpha mode. tint: sRGB. */
void sk_place(vec2 corner, vec4 pos, vec4 size, vec4 source, vec3 right_axis, vec4 up_axis, vec4 tint) {
    vec3 right = right_axis;
    vec3 up = up_axis.xyz;
    if (pos.w < 0.5) {
        right = sk_camera_right.xyz;
        up = sk_camera_up.xyz;
    } else if (pos.w < 1.5) {
        right = sk_upright.xyz;
        up = vec3(0.0, 1.0, 0.0);
    }
    vec2 local = vec2((corner.x + 0.5 - size.z) * size.x, (corner.y - 0.5 + size.w) * size.y);
    vec3 world = pos.xyz + right * local.x + up * local.y;
    gl_Position = sk_view_proj * vec4(world, 1.0);
    sk_world_pos = world;
    sk_normal = cross(right, up);
    sk_tangent = vec4(right, 1.0);
    sk_uv0 = vec2(mix(source.x, source.z, corner.x + 0.5), mix(source.y, source.w, 0.5 - corner.y));
    sk_uv1 = vec2(corner.x + 0.5, 0.5 - corner.y);
    sk_color = vec4(sk_srgb_to_linear(tint.rgb), tint.a);
    sk_sprite_alpha = up_axis.w;
}
@end

@block sk_vs_sprite_main
layout(location=0) in vec2 corner;
layout(location=1) in vec4 inst_pos;
layout(location=2) in vec4 inst_size;
layout(location=3) in vec4 inst_uv;
layout(location=4) in vec3 inst_right;
layout(location=5) in vec4 inst_up;
layout(location=6) in vec4 inst_color;
void main() {
    sk_place(corner, inst_pos, inst_size, inst_uv, inst_right, inst_up, inst_color);
}
@end

@block sk_vs_sprite_pulled_main
layout(binding=4) uniform sk_sprite_batch {
    vec4 sk_batch; /* x: the batch's first sprite */
};
layout(binding=11) uniform texture2D sk_sprite_data;
layout(binding=11) uniform sampler sk_sprite_data_smp;
@image_sample_type sk_sprite_data unfilterable_float
@sampler_type sk_sprite_data_smp nonfiltering
layout(location=0) in vec2 corner;
vec4 sk_sprite_texel(int sprite, int k) {
    return texelFetch(sampler2D(sk_sprite_data, sk_sprite_data_smp), ivec2((sprite % 256) * 6 + k, sprite / 256), 0);
}
void main() {
    int sprite = int(sk_batch.x) + gl_InstanceIndex;
    sk_place(corner, sk_sprite_texel(sprite, 0), sk_sprite_texel(sprite, 1), sk_sprite_texel(sprite, 2),
             sk_sprite_texel(sprite, 3).xyz, sk_sprite_texel(sprite, 4), sk_sprite_texel(sprite, 5));
}
@end

/* --------------------------------------------------------------- fragment ---- */

@block sk_surface
@include_block sk_color
layout(binding=1) uniform sk_frame {
    vec4 sk_camera_time;    /* xyz camera position, w seconds */
    vec4 sk_tint;           /* the model's tint, linear rgba */
    vec4 sk_ambient_count;  /* rgb ambient (linear), w number of lights */
    vec4 sk_output_params;  /* x alpha cutoff (0 none), y tone mapping (0 none, 1 neutral, 2 ACES), z exposure scale */
    vec4 sk_light_pos_range[8];  /* xyz position, w range (0 unlimited) */
    vec4 sk_light_dir_type[8];   /* xyz direction the light travels, w type (0 directional, 1 point, 2 spot) */
    vec4 sk_light_radiance[8];   /* rgb color x intensity, linear */
    vec4 sk_light_spot[8];       /* x cos(inner angle), y cos(outer angle) */
    vec4 sk_env;                 /* x intensity (0 none), y the cubemap's last mip, z/w cos/sin of its rotation */
    vec4 sk_sh[9];               /* environment irradiance / pi, spherical harmonics (xyz) */
};
layout(binding=8) uniform textureCube sk_env_tex;
layout(binding=8) uniform sampler sk_env_smp;
layout(binding=9) uniform texture2D sk_brdf_tex;
layout(binding=9) uniform sampler sk_brdf_smp;
layout(location=0) in vec3 sk_world_pos;
layout(location=1) in vec3 sk_normal;
layout(location=2) in vec4 sk_tangent;
layout(location=3) in vec2 sk_uv0;
layout(location=4) in vec2 sk_uv1;
layout(location=5) in vec4 sk_color;
layout(location=6) in float sk_sprite_alpha;
layout(binding=10) uniform texture2D sk_sprite_tex;
layout(binding=10) uniform sampler sk_sprite_smp;
out vec4 sk_frag_color;

vec4 sk_sprite_color() {
    vec4 t = texture(sampler2D(sk_sprite_tex, sk_sprite_smp), sk_uv0); /* white on models */
    return vec4(sk_srgb_to_linear(t.rgb), t.a) * sk_color;
}

float sk_time() { return sk_camera_time.w; }
vec3 sk_camera_position() { return sk_camera_time.xyz; }
vec3 sk_ambient() { return sk_ambient_count.rgb; }
int sk_light_count() { return int(sk_ambient_count.w + 0.5); }

/* The same falloff as built-in materials (src/sk_light.c). */
vec3 sk_light(int i, vec3 pos, out vec3 to_light) {
    int type = int(sk_light_dir_type[i].w + 0.5);
    if (type == 0) {
        to_light = -normalize(sk_light_dir_type[i].xyz);
        return sk_light_radiance[i].rgb;
    }
    vec3 d = pos - sk_light_pos_range[i].xyz;
    float dist = length(d);
    vec3 dir = dist > 1e-6 ? d / dist : vec3(0.0, -1.0, 0.0);
    to_light = -dir;
    float falloff = 1.0 / max(dist * dist, 0.01);
    float range = sk_light_pos_range[i].w;
    if (range > 0.0) {
        float r = dist / range;
        float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
        falloff *= w * w;
    }
    if (type == 2) {
        float cos_angle = dot(dir, normalize(sk_light_dir_type[i].xyz));
        float cos_inner = sk_light_spot[i].x;
        float cos_outer = sk_light_spot[i].y;
        falloff *= cos_inner - cos_outer <= 1e-6 ? (cos_angle >= cos_outer ? 1.0 : 0.0)
                                                 : smoothstep(cos_outer, cos_inner, cos_angle);
    }
    return sk_light_radiance[i].rgb * falloff;
}

float sk_environment_intensity() { return sk_env.x; }

/* A world direction in the environment's frame (rotated by -rotation around +y). */
vec3 sk_environment_dir(vec3 dir) {
    return vec3(sk_env.z * dir.x - sk_env.w * dir.z, dir.y, sk_env.w * dir.x + sk_env.z * dir.z);
}

vec3 sk_environment_diffuse(vec3 n) {
    vec3 d = sk_environment_dir(n);
    vec3 irradiance = sk_sh[0].xyz * 0.282095
                    + sk_sh[1].xyz * (0.488603 * d.y)
                    + sk_sh[2].xyz * (0.488603 * d.z)
                    + sk_sh[3].xyz * (0.488603 * d.x)
                    + sk_sh[4].xyz * (1.092548 * d.x * d.y)
                    + sk_sh[5].xyz * (1.092548 * d.y * d.z)
                    + sk_sh[6].xyz * (0.315392 * (3.0 * d.z * d.z - 1.0))
                    + sk_sh[7].xyz * (1.092548 * d.x * d.z)
                    + sk_sh[8].xyz * (0.546274 * (d.x * d.x - d.y * d.y));
    return max(irradiance, vec3(0.0)) * sk_env.x;
}

vec3 sk_environment_specular(vec3 n, vec3 v, float roughness) {
    vec3 r = sk_environment_dir(reflect(-v, n));
    return textureLod(samplerCube(sk_env_tex, sk_env_smp), r, clamp(roughness, 0.0, 1.0) * sk_env.y).rgb * sk_env.x;
}

vec2 sk_environment_brdf(float n_dot_v, float roughness) {
    return texture(sampler2D(sk_brdf_tex, sk_brdf_smp), vec2(clamp(n_dot_v, 0.0, 1.0), clamp(roughness, 0.0, 1.0))).rg;
}

vec3 sk_tonemap_neutral(vec3 color) {
    const float start_compression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < start_compression) {
        return color;
    }
    const float d = 1.0 - start_compression;
    float new_peak = 1.0 - d * d / (peak + d - start_compression);
    color *= new_peak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return mix(color, vec3(new_peak), g);
}

vec3 sk_tonemap_aces(vec3 color) {
    color *= 0.6;
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

void sk_output(vec3 color, float alpha) {
    color *= sk_tint.rgb;
    alpha *= sk_tint.a;
    /* the material's cutoff, or a sprite's (its alpha mode: > 0 a cutoff, < 0 opaque) */
    float cutoff = max(sk_output_params.x, max(sk_sprite_alpha, 0.0));
    if (alpha < cutoff) {
        discard;
    }
    if (sk_sprite_alpha != 0.0) {
        alpha = 1.0; /* opaque and masked sprites */
    }
    color *= sk_output_params.z;
    int mode = int(sk_output_params.y + 0.5);
    if (mode == 1) {
        color = sk_tonemap_neutral(color);
    } else if (mode == 2) {
        color = sk_tonemap_aces(color);
    }
    sk_frag_color = vec4(sk_linear_to_srgb(color), alpha);
}
@end
