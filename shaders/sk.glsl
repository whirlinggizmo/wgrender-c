/* libsk custom shaders: what libsk gives a material's shader (docs/PLAN-materials.md,
 * "Custom shaders"). tools/shaderpack.py puts this file in front of yours, adds libsk's
 * vertex shaders (static and skinned models) and compiles the result for every backend
 * into a .skshader file (sk_shader_create).
 *
 * Your file has a fragment shader named `fs`, and may have a vertex hook:
 *
 *     @block vertex                           (optional)
 *     void sk_vertex(inout vec3 position, inout vec3 normal) { ... }   object space,
 *     @end                                    before skinning; sk_time() is available
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
 * Parameters: the members of uniform block binding 2 in the fragment shader and
 * binding 3 in the vertex hook, set by name with sk_material_set_float / _vec2 /
 * _vec3 / _vec4 / _int / _color (float, vec2, vec3, vec4, int; not arrays or
 * matrices). Names must differ between the two blocks. Colors given as sk_color_t
 * are converted to linear, like built-in materials. Textures: each texture2D (up to
 * 8, fragment shader, bindings 0-7) is set by its name with sk_material_set_texture,
 * and sampled with the sampler it's paired with in texture(sampler2D(...)), set up
 * by sk_material_set_texture_sampling. A texture that isn't set is white.
 *
 * Fragment inputs (sk_surface), world space:
 *   sk_world_pos   vec3   the surface point
 *   sk_normal      vec3   interpolated vertex normal (not normalized; facing out of
 *                         the front face)
 *   sk_tangent     vec4   xyz tangent, w bitangent sign (glTF)
 *   sk_uv0, sk_uv1 vec2   texture coordinate sets 0 and 1
 *   sk_color       vec4   vertex color, linear (white when the mesh has none)
 * and functions:
 *   sk_time()             seconds since the program started
 *   sk_camera_position()  world space
 *   sk_ambient()          the scene's ambient light, linear rgb (0 outside a scene)
 *   sk_light_count()      lights reaching this model (0..8)
 *   sk_light(i, pos, out to_light)   light i's radiance arriving at world position
 *                         `pos` (linear rgb, attenuated), and the unit direction from
 *                         `pos` toward the light
 *   sk_srgb_to_linear(c), sk_linear_to_srgb(c)
 *   sk_output(color, alpha)   write the pixel: `color` is linear rgb. Applies the
 *                         model's tint, the material's alpha cutoff (SK_ALPHA_MASK),
 *                         and the scene's exposure and tone mapping, then encodes sRGB.
 *                         Call it once, at the end.
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
};
layout(location=0) in vec3 sk_world_pos;
layout(location=1) in vec3 sk_normal;
layout(location=2) in vec4 sk_tangent;
layout(location=3) in vec2 sk_uv0;
layout(location=4) in vec2 sk_uv1;
layout(location=5) in vec4 sk_color;
out vec4 sk_frag_color;

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
    if (alpha < sk_output_params.x) {
        discard;
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
