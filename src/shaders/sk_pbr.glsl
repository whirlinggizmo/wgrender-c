/* libsk's surface shading, shared by models (sk_model.glsl) and lit sprites
 * (sk_sprite.glsl): the glTF metallic-roughness BRDF, the scene's lights and
 * environment, tone mapping. Regen both with `make shaders`.
 *
 * A shader that includes sk_pbr_surface and sk_pbr_main needs a vertex shader writing
 * v_world_pos, v_normal, v_tangent, v_uv0, v_uv1, v_color and v_alpha_mode (a sprite's
 * alpha mode: > 0 a cutoff, < 0 opaque, 0 as it is; models write 0).
 */

@block sk_pbr_color
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

@block sk_pbr_surface
@include_block sk_pbr_color
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
in float v_alpha_mode; /* sprites: their alpha mode (models write 0) */
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
@end

@block sk_pbr_main
void main() {
    vec4 base_sample = texture(sampler2D(base_color_tex, base_color_smp), tex_uv(0));
    vec4 base = vec4(srgb_to_linear(base_sample.rgb), base_sample.a) * u_base_color * v_color;
    float cutoff = max(u_material.x, max(v_alpha_mode, 0.0));
    if (base.a < cutoff) {
        discard;
    }
    if (v_alpha_mode != 0.0) {
        base.a = 1.0; /* opaque and masked sprites */
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
