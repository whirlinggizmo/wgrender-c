/* sk_model shaders — static + GPU-skinned, glTF metallic-roughness materials.
 * Authored once in annotated (Vulkan-style) GLSL; sokol-shdc generates
 * sk_model.glsl.h with GL core / WebGL2 / WebGPU variants. Regen: `make shaders`.
 *
 * Vertex colors (glTF COLOR_0, linear rgba; white when absent) multiply the base color.
 *
 * Uniform blocks mirror the C structs in sk_model.c (std140):
 *   vs_params      = mvp, model, normal_mat
 *   vs_skin_params = mvp, model, normal_mat, skin_base (x: where this model's joint
 *                    matrices start in the frame's joint texture, 4 texels each, 256
 *                    matrices a row; src/sk_model.c uploads it once a frame)
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
 *   u_env             environment (docs/PLAN-environment.md): x intensity (0 = none),
 *                     y the prefiltered cubemap's last mip (roughness 1), z/w cos/sin
 *                     of its rotation around +y
 *   u_sh[9]           environment irradiance / pi as spherical harmonics (xyz)
 *   u_tonemap         x mode (0 none, 1 Khronos PBR Neutral, 2 ACES), y exposure
 *                     scale (2^EV). Mode 0 and scale 1 outside scenes.
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
    vec4 skin_base; /* x: this model's first joint matrix */
};
layout(binding=7) uniform texture2D joint_tex;
layout(binding=7) uniform sampler joint_smp;
@image_sample_type joint_tex unfilterable_float
@sampler_type joint_smp nonfiltering

mat4 joint_at(int index) {
    int m = int(skin_base.x) + index;
    ivec2 t = ivec2((m % 256) * 4, m / 256);
    return mat4(texelFetch(sampler2D(joint_tex, joint_smp), t, 0),
                texelFetch(sampler2D(joint_tex, joint_smp), t + ivec2(1, 0), 0),
                texelFetch(sampler2D(joint_tex, joint_smp), t + ivec2(2, 0), 0),
                texelFetch(sampler2D(joint_tex, joint_smp), t + ivec2(3, 0), 0));
}
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
    mat4 skin = weights.x * joint_at(int(joints.x))
              + weights.y * joint_at(int(joints.y))
              + weights.z * joint_at(int(joints.z))
              + weights.w * joint_at(int(joints.w));
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

@block color
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

/* Khronos PBR Neutral (https://github.com/KhronosGroup/ToneMapping) */
vec3 tonemap_neutral(vec3 color) {
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

/* ACES filmic curve fit (Narkowicz 2015) */
vec3 tonemap_aces(vec3 color) {
    color *= 0.6;
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

/* Linear scene color to the framebuffer's sRGB, with exposure and tone mapping. */
vec3 to_display(vec3 color, vec4 tonemap) {
    color *= tonemap.y;
    int mode = int(tonemap.x + 0.5);
    if (mode == 1) {
        color = tonemap_neutral(color);
    } else if (mode == 2) {
        color = tonemap_aces(color);
    }
    return linear_to_srgb(color);
}

/* A world direction in the environment's frame (rotated by -rotation around +y). */
vec3 env_dir(vec3 dir, vec4 env) {
    return vec3(env.z * dir.x - env.w * dir.z, dir.y, env.w * dir.x + env.z * dir.z);
}
@end

@fs fs
@include_block color
/* Three blocks, not one: the material changes every draw (224 bytes), the scene and
 * its lights only when the camera, environment or a model's lights do. Sending all of
 * it per draw cost ~950 bytes a draw, most of a crowded frame's CPU (src/sk_model.c). */
layout(binding=1) uniform fs_params {
    vec4 u_base_color;
    vec4 u_emissive;
    vec4 u_pbr;
    vec4 u_material;
    vec4 u_uv_row0[5];
    vec4 u_uv_row1[5];
};
layout(binding=2) uniform fs_scene {
    vec4 u_camera_pos;
    vec4 u_ambient;
    vec4 u_env;
    vec4 u_sh[9];
    vec4 u_tonemap;
};
layout(binding=3) uniform fs_lights {
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
layout(binding=5) uniform textureCube env_tex;
layout(binding=6) uniform texture2D brdf_tex;
layout(binding=5) uniform sampler env_smp;
layout(binding=6) uniform sampler brdf_smp;
in vec3 v_normal;
in vec4 v_tangent;
in vec2 v_uv0;
in vec2 v_uv1;
in vec4 v_color;
in vec3 v_world_pos;
out vec4 frag_color;

/* Environment irradiance / pi at a direction (environment frame). */
vec3 eval_sh(vec3 n) {
    return u_sh[0].xyz * 0.282095
         + u_sh[1].xyz * (0.488603 * n.y)
         + u_sh[2].xyz * (0.488603 * n.z)
         + u_sh[3].xyz * (0.488603 * n.x)
         + u_sh[4].xyz * (1.092548 * n.x * n.y)
         + u_sh[5].xyz * (1.092548 * n.y * n.z)
         + u_sh[6].xyz * (0.315392 * (3.0 * n.z * n.z - 1.0))
         + u_sh[7].xyz * (1.092548 * n.x * n.z)
         + u_sh[8].xyz * (0.546274 * (n.x * n.x - n.y * n.y));
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
        frag_color = vec4(to_display(base.rgb, u_tonemap), base.a); /* unlit */
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

    if (u_env.x > 0.0) {
        /* split-sum image-based lighting: diffuse from SH irradiance, specular from the
         * prefiltered cubemap mip for this roughness and the BRDF table */
        float lod_roughness = sqrt(clamp(alpha, 0.0, 1.0)); /* the perceptual roughness used for the mips */
        vec3 irradiance = max(eval_sh(env_dir(n, u_env)), vec3(0.0));
        vec3 r = env_dir(reflect(-v, n), u_env);
        vec3 prefiltered = textureLod(samplerCube(env_tex, env_smp), r, lod_roughness * u_env.y).rgb;
        vec2 ab = texture(sampler2D(brdf_tex, brdf_smp), vec2(n_dot_v, lod_roughness)).rg;
        color += (irradiance * c_diff + prefiltered * (f0 * ab.x + ab.y)) * ao * u_env.x;
    }

    color += u_emissive.rgb * srgb_to_linear(texture(sampler2D(emissive_tex, emissive_smp), tex_uv(4)).rgb);
    frag_color = vec4(to_display(color, u_tonemap), base.a);
}
@end

/* Background (skybox): a full-screen triangle at the far plane; each pixel looks up
 * the environment in its view direction. */
@vs vs_background
in vec2 bg_position;
out vec2 v_ndc;
void main() {
    gl_Position = vec4(bg_position, 1.0, 1.0);
    v_ndc = bg_position;
}
@end

@fs fs_background
@include_block color
layout(binding=0) uniform bg_params {
    mat4 inv_view_proj;
    vec4 bg_env;      /* x intensity, y mip (blur), z/w cos/sin of rotation */
    vec4 bg_tonemap;  /* as u_tonemap */
};
layout(binding=0) uniform textureCube bg_tex;
layout(binding=0) uniform sampler bg_smp;
in vec2 v_ndc;
out vec4 frag_color;
void main() {
    vec4 near_point = inv_view_proj * vec4(v_ndc, -1.0, 1.0);
    vec4 far_point = inv_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(far_point.xyz / far_point.w - near_point.xyz / near_point.w);
    vec3 radiance = textureLod(samplerCube(bg_tex, bg_smp), env_dir(dir, bg_env), bg_env.y).rgb * bg_env.x;
    frag_color = vec4(to_display(radiance, bg_tonemap), 1.0);
}
@end

@program model_static vs_static fs
@program model_skinned vs_skinned fs
@program background vs_background fs_background
