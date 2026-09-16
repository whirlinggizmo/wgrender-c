/* sk_model shaders — static + GPU-skinned, lit (directional + ambient) textured.
 * Authored once in annotated (Vulkan-style) GLSL; sokol-shdc generates
 * sk_model.glsl.h with GL core / WebGL2 / WebGPU variants. Regen: `make shaders`.
 *
 * Uniform blocks mirror the C structs in sk_model.c (std140):
 *   vs_params      = mvp, model
 *   vs_skin_params = mvp, model, joints_mat[128]
 *   fs_params      = u_tint, u_material, u_ambient, u_light_*[8]
 *
 * u_material.x is the alpha cutoff: fragments with alpha below it are discarded
 * (glTF alphaMode MASK). 0 for OPAQUE and BLEND, so nothing is discarded.
 *
 * Lighting (docs/PLAN-lighting.md), world space, diffuse only:
 *   u_ambient.rgb     ambient color * intensity
 *   u_ambient.w       1 = lit, 0 = unlit (output base color * tint)
 *   u_material.y      number of lights in the arrays (0..8)
 *   u_light_pos_range[i]  xyz position, w range (0 = unlimited)
 *   u_light_dir_type[i]   xyz direction the light travels, w type (0 dir, 1 point, 2 spot)
 *   u_light_radiance[i]   rgb color * intensity
 *   u_light_spot[i]       x cos(inner), y cos(outer)
 * The attenuation and cone formulas match sk_light_attenuation and
 * sk_light_spot_factor in src/sk_light.c.
 */

@vs vs_static
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
};
in vec3 position;
in vec3 normal;
in vec2 texcoord0;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_world_pos;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    v_normal = mat3(model) * normal;
    v_uv = texcoord0;
    v_world_pos = (model * vec4(position, 1.0)).xyz;
}
@end

@vs vs_skinned
layout(binding=0) uniform vs_skin_params {
    mat4 mvp;
    mat4 model;
    mat4 joints_mat[128];
};
in vec3 position;
in vec3 normal;
in vec2 texcoord0;
in vec4 joints;
in vec4 weights;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_world_pos;
void main() {
    mat4 skin = weights.x * joints_mat[int(joints.x)]
              + weights.y * joints_mat[int(joints.y)]
              + weights.z * joints_mat[int(joints.z)]
              + weights.w * joints_mat[int(joints.w)];
    vec4 sp = skin * vec4(position, 1.0);
    gl_Position = mvp * sp;
    v_normal = mat3(model) * mat3(skin) * normal;
    v_uv = texcoord0;
    v_world_pos = (model * sp).xyz;
}
@end

@fs fs
layout(binding=1) uniform fs_params {
    vec4 u_tint;
    vec4 u_material;
    vec4 u_ambient;
    vec4 u_light_pos_range[8];
    vec4 u_light_dir_type[8];
    vec4 u_light_radiance[8];
    vec4 u_light_spot[8];
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_world_pos;
out vec4 frag_color;

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
    vec4 base = texture(sampler2D(tex, smp), v_uv) * u_tint;
    if (base.a < u_material.x) {
        discard;
    }
    if (u_ambient.w < 0.5) {
        frag_color = base; /* unlit */
        return;
    }
    vec3 n = normalize(v_normal);
    vec3 lit = u_ambient.rgb;
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
        lit += u_light_radiance[i].rgb * max(dot(n, -light_dir), 0.0) * falloff;
    }
    frag_color = vec4(base.rgb * lit, base.a);
}
@end

@program model_static vs_static fs
@program model_skinned vs_skinned fs
