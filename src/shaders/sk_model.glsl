/* sk_model shaders — static + GPU-skinned, lit (directional + ambient) textured.
 * Authored once in annotated (Vulkan-style) GLSL; sokol-shdc generates
 * sk_model.glsl.h with GL core / WebGL2 / WebGPU variants. Regen: `make shaders`.
 *
 * Uniform blocks mirror the C structs in sk_model.c (std140):
 *   vs_params      = mvp, model
 *   vs_skin_params = mvp, model, joints_mat[128]
 *   fs_params      = u_light_dir, u_tint, u_ambient, u_material
 *
 * u_material.x is the alpha cutoff: fragments with alpha below it are discarded
 * (glTF alphaMode MASK). 0 for OPAQUE and BLEND, so nothing is discarded.
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
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    v_normal = mat3(model) * normal;
    v_uv = texcoord0;
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
void main() {
    mat4 skin = weights.x * joints_mat[int(joints.x)]
              + weights.y * joints_mat[int(joints.y)]
              + weights.z * joints_mat[int(joints.z)]
              + weights.w * joints_mat[int(joints.w)];
    vec4 sp = skin * vec4(position, 1.0);
    gl_Position = mvp * sp;
    v_normal = mat3(model) * mat3(skin) * normal;
    v_uv = texcoord0;
}
@end

@fs fs
layout(binding=1) uniform fs_params {
    vec4 u_light_dir;
    vec4 u_tint;
    vec4 u_ambient;
    vec4 u_material;
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
in vec3 v_normal;
in vec2 v_uv;
out vec4 frag_color;
void main() {
    vec3 n = normalize(v_normal);
    vec3 ld = normalize(u_light_dir.xyz);
    float d = max(dot(n, -ld), 0.0);
    float a = u_ambient.x;
    float lit = a + (1.0 - a) * d;
    vec4 base = texture(sampler2D(tex, smp), v_uv) * u_tint;
    if (base.a < u_material.x) {
        discard;
    }
    frag_color = vec4(base.rgb * lit, base.a);
}
@end

@program model_static vs_static fs
@program model_skinned vs_skinned fs
