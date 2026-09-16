/* sk_model shaders — static + GPU-skinned, glTF metallic-roughness materials.
 * Authored once in annotated (Vulkan-style) GLSL; sokol-shdc generates
 * sk_model.glsl.h with GL core / WebGL2 / WebGPU variants. Regen: `make shaders`.
 *
 * Vertex colors (glTF COLOR_0, linear rgba; white when absent) multiply the base color.
 *
 * Uniform blocks mirror the C structs in sk_model.c (std140):
 *   vs_params      = mvp, model, normal_mat
 *   vs_skin_params = mvp, model, normal_mat, joints_mat[128]
 *   fs_params      = material, camera and lights (below)
 *
 * Color space (docs/PLAN-materials.md): the framebuffer holds sRGB values.
 * Color textures are decoded from sRGB, factors and light radiance are linear,
 * lighting happens in linear space, and the result is encoded back to sRGB.
 *
 * fs_params:
 *   u_base_color      linear rgba: material base color x model tint
 *   u_emissive        rgb linear emissive, w normal scale
 *   u_pbr             x metallic, y roughness, z occlusion strength,
 *                     w 1 = lit (PBR in a lit scene), 0 = unlit (base color only)
 *   u_material        x alpha cutoff (0 = no alpha test), y number of lights (0..8)
 *   u_camera_pos      xyz camera position, world space
 *   u_ambient         rgb ambient color * intensity (linear)
 *   u_uv_row0[i], u_uv_row1[i]  texture slot i (base color, metallic-roughness,
 *                     normal, occlusion, emissive): xyz the rows of its 2x3 texture
 *                     transform, u_uv_row0[i].w its texture coordinate set (0 or 1)
 *   u_light_pos_range[i]  xyz position, w range (0 = unlimited)
 *   u_light_dir_type[i]   xyz direction the light travels, w type (0 dir, 1 point, 2 spot)
 *   u_light_radiance[i]   rgb color * intensity (linear)
 *   u_light_spot[i]       x cos(inner), y cos(outer)
 * The attenuation and cone formulas match sk_light_attenuation and
 * sk_light_spot_factor in src/sk_light.c. The BRDF follows the glTF 2.0
 * specification, appendix B (Lambert diffuse, GGX / Smith height-correlated
 * specular, Schlick Fresnel).
 */

@vs vs_static
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
    mat4 normal_mat; /* inverse transpose of model */
};
in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec2 texcoord1;
in vec4 tangent;
in vec4 color0;
out vec3 v_normal;
out vec4 v_tangent;
out vec2 v_uv0;
out vec2 v_uv1;
out vec4 v_color;
out vec3 v_world_pos;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    v_normal = mat3(normal_mat) * normal;
    v_tangent = vec4(mat3(model) * tangent.xyz, tangent.w);
    v_uv0 = texcoord0;
    v_uv1 = texcoord1;
    v_color = color0;
    v_world_pos = (model * vec4(position, 1.0)).xyz;
}
@end

@vs vs_skinned
layout(binding=0) uniform vs_skin_params {
    mat4 mvp;
    mat4 model;
    mat4 normal_mat;
    mat4 joints_mat[128];
};
in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec2 texcoord1;
in vec4 tangent;
in vec4 color0;
in vec4 joints;
in vec4 weights;
out vec3 v_normal;
out vec4 v_tangent;
out vec2 v_uv0;
out vec2 v_uv1;
out vec4 v_color;
out vec3 v_world_pos;
void main() {
    mat4 skin = weights.x * joints_mat[int(joints.x)]
              + weights.y * joints_mat[int(joints.y)]
              + weights.z * joints_mat[int(joints.z)]
              + weights.w * joints_mat[int(joints.w)];
    vec4 sp = skin * vec4(position, 1.0);
    gl_Position = mvp * sp;
    /* joints are rigid transforms (plus uniform scale), so mat3(skin) keeps normals perpendicular */
    v_normal = mat3(normal_mat) * (mat3(skin) * normal);
    v_tangent = vec4(mat3(model) * (mat3(skin) * tangent.xyz), tangent.w);
    v_uv0 = texcoord0;
    v_uv1 = texcoord1;
    v_color = color0;
    v_world_pos = (model * sp).xyz;
}
@end

@fs fs
layout(binding=1) uniform fs_params {
    vec4 u_base_color;
    vec4 u_emissive;
    vec4 u_pbr;
    vec4 u_material;
    vec4 u_camera_pos;
    vec4 u_ambient;
    vec4 u_uv_row0[5];
    vec4 u_uv_row1[5];
    vec4 u_light_pos_range[8];
    vec4 u_light_dir_type[8];
    vec4 u_light_radiance[8];
    vec4 u_light_spot[8];
};
layout(binding=0) uniform texture2D base_color_tex;
layout(binding=1) uniform texture2D metallic_roughness_tex;
layout(binding=2) uniform texture2D normal_tex;
layout(binding=3) uniform texture2D occlusion_tex;
layout(binding=4) uniform texture2D emissive_tex;
layout(binding=0) uniform sampler base_color_smp;
layout(binding=1) uniform sampler metallic_roughness_smp;
layout(binding=2) uniform sampler normal_smp;
layout(binding=3) uniform sampler occlusion_smp;
layout(binding=4) uniform sampler emissive_smp;
in vec3 v_normal;
in vec4 v_tangent;
in vec2 v_uv0;
in vec2 v_uv1;
in vec4 v_color;
in vec3 v_world_pos;
out vec4 frag_color;

const float PI = 3.14159265359;

vec3 srgb_to_linear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((max(c, vec3(0.04045)) + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 linear_to_srgb(vec3 c) {
    c = clamp(c, vec3(0.0), vec3(1.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0031308)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

/* Texture coordinates for texture slot i: its coordinate set, then its transform. */
vec2 tex_uv(int i) {
    vec3 uv = vec3(u_uv_row0[i].w < 0.5 ? v_uv0 : v_uv1, 1.0);
    return vec2(dot(u_uv_row0[i].xyz, uv), dot(u_uv_row1[i].xyz, uv));
}

float attenuation(float d, float range) {
    float inv_sq = 1.0 / max(d * d, 0.01);
    if (range <= 0.0) {
        return inv_sq;
    }
    float r = d / range;
    float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
    return w * w * inv_sq;
}

float spot_factor(float cos_angle, float cos_inner, float cos_outer) {
    if (cos_inner - cos_outer <= 1e-6) {
        return cos_angle >= cos_outer ? 1.0 : 0.0;
    }
    return smoothstep(cos_outer, cos_inner, cos_angle);
}

void main() {
    vec4 base_sample = texture(sampler2D(base_color_tex, base_color_smp), tex_uv(0));
    vec4 base = vec4(srgb_to_linear(base_sample.rgb), base_sample.a) * u_base_color * v_color;
    if (base.a < u_material.x) {
        discard;
    }
    if (u_pbr.w < 0.5) {
        frag_color = vec4(linear_to_srgb(base.rgb), base.a); /* unlit */
        return;
    }

    /* shading frame; back faces of double-sided surfaces face the viewer */
    vec3 n = normalize(v_normal);
    vec3 t = v_tangent.xyz - n * dot(n, v_tangent.xyz);
    float face = gl_FrontFacing ? 1.0 : -1.0;
    if (dot(t, t) > 1e-8) {
        t = normalize(t);
        vec3 b = cross(n, t) * v_tangent.w;
        vec3 tn = texture(sampler2D(normal_tex, normal_smp), tex_uv(2)).xyz * 2.0 - 1.0;
        tn.xy *= u_emissive.w;
        n = normalize(mat3(t, b, n) * tn);
    }
    n *= face;

    vec3 mr = texture(sampler2D(metallic_roughness_tex, metallic_roughness_smp), tex_uv(1)).rgb;
    float metallic = clamp(u_pbr.x * mr.b, 0.0, 1.0);
    float roughness = clamp(u_pbr.y * mr.g, 0.03, 1.0);
    float alpha = roughness * roughness;
    float alpha_sq = alpha * alpha;
    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    vec3 c_diff = base.rgb * (1.0 - metallic);

    vec3 v = normalize(u_camera_pos.xyz - v_world_pos);
    float n_dot_v = clamp(abs(dot(n, v)), 1e-4, 1.0);

    float ao = 1.0 + u_pbr.z * (texture(sampler2D(occlusion_tex, occlusion_smp), tex_uv(3)).r - 1.0);
    vec3 color = u_ambient.rgb * (c_diff + f0) * ao;

    int count = int(u_material.y);
    for (int i = 0; i < 8; i++) {
        if (i >= count) {
            break;
        }
        vec3 light_dir;
        float falloff = 1.0;
        int type = int(u_light_dir_type[i].w + 0.5);
        if (type == 0) {
            light_dir = normalize(u_light_dir_type[i].xyz);
        } else {
            vec3 to_frag = v_world_pos - u_light_pos_range[i].xyz;
            float d = length(to_frag);
            light_dir = d > 1e-6 ? to_frag / d : vec3(0.0, -1.0, 0.0);
            falloff = attenuation(d, u_light_pos_range[i].w);
            if (type == 2) {
                falloff *= spot_factor(dot(light_dir, normalize(u_light_dir_type[i].xyz)),
                                       u_light_spot[i].x, u_light_spot[i].y);
            }
        }
        vec3 l = -light_dir;
        float n_dot_l = dot(n, l);
        if (n_dot_l <= 0.0 || falloff <= 0.0) {
            continue;
        }
        vec3 h = normalize(l + v);
        float n_dot_h = clamp(dot(n, h), 0.0, 1.0);
        float v_dot_h = clamp(dot(v, h), 0.0, 1.0);

        vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0);
        float dd = n_dot_h * n_dot_h * (alpha_sq - 1.0) + 1.0;
        float distribution = alpha_sq / (PI * dd * dd);
        float gv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - alpha_sq) + alpha_sq);
        float gl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - alpha_sq) + alpha_sq);
        float visibility = 0.5 / max(gv + gl, 1e-6);

        vec3 diffuse = (1.0 - fresnel) * c_diff / PI;
        vec3 specular = fresnel * distribution * visibility;
        color += u_light_radiance[i].rgb * falloff * n_dot_l * (diffuse + specular);
    }

    color += u_emissive.rgb * srgb_to_linear(texture(sampler2D(emissive_tex, emissive_smp), tex_uv(4)).rgb);
    frag_color = vec4(linear_to_srgb(color), base.a);
}
@end

@program model_static vs_static fs
@program model_skinned vs_skinned fs
