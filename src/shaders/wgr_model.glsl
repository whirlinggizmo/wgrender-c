/* wgr_model shaders — static + GPU-skinned, glTF metallic-roughness materials.
 * Authored once in annotated (Vulkan-style) GLSL; sokol-shdc generates
 * wgr_model.glsl.h with GL core / WebGL2 / WebGPU variants. Regen: `make shaders`.
 *
 * Vertex colors (glTF COLOR_0, linear rgba; white when absent) multiply the base color.
 *
 * Uniform blocks mirror the C structs in wgr_model.c (std140):
 *   vs_params      = mvp, model, normal_mat
 *   vs_skin_params = mvp, model, normal_mat, skin_base (x: where this model's joint
 *                    matrices start in the frame's joint texture, 4 texels each, 256
 *                    matrices a row; src/wgr_model.c uploads it once a frame)
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
 * The attenuation and cone formulas match wgri_light_attenuation and
 * wgri_light_spot_factor in src/wgr_light.c. The BRDF follows the glTF 2.0
 * specification, appendix B (Lambert diffuse, GGX / Smith height-correlated
 * specular, Schlick Fresnel).
 */


@include wgr_instance.glsl

@vs vs_static
@include_block wgr_instance_data
layout(binding=0) uniform vs_params {
    mat4 view_proj;
    vec4 instance_base; /* x: this draw's first record */
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
out float v_alpha_mode; /* sprites use it (wgr_pbr.glsl); models don't */
void main() {
    int record = int(instance_base.x) + gl_InstanceIndex;
    mat4 model = instance_model(record);
    vec4 world = model * vec4(position, 1.0);
    v_alpha_mode = 0.0;
    gl_Position = view_proj * world;
    v_normal = instance_normal(record) * normal;
    v_tangent = vec4(mat3(model) * tangent.xyz, tangent.w);
    v_uv0 = texcoord0;
    v_uv1 = texcoord1;
    v_color = color0 * instance_tint(record); /* the placement's tint, already linear */
    v_world_pos = world.xyz;
}
@end

@vs vs_skinned
@include_block wgr_instance_data
layout(binding=0) uniform vs_skin_params {
    mat4 view_proj;
    vec4 instance_base;
};
layout(binding=7) uniform texture2D joint_tex;
layout(binding=7) uniform sampler joint_smp;
@image_sample_type joint_tex unfilterable_float
@sampler_type joint_smp nonfiltering

mat4 joint_at(int base, int index) {
    int m = base + index;
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
out float v_alpha_mode;
void main() {
    int record = int(instance_base.x) + gl_InstanceIndex;
    int joint_base = int(instance_extra(record).x);
    mat4 model = instance_model(record);
    mat4 skin = weights.x * joint_at(joint_base, int(joints.x))
              + weights.y * joint_at(joint_base, int(joints.y))
              + weights.z * joint_at(joint_base, int(joints.z))
              + weights.w * joint_at(joint_base, int(joints.w));
    vec4 sp = skin * vec4(position, 1.0);
    vec4 world = model * sp;
    v_alpha_mode = 0.0;
    gl_Position = view_proj * world;
    /* joints are rigid transforms (plus uniform scale), so mat3(skin) keeps normals perpendicular */
    v_normal = instance_normal(record) * (mat3(skin) * normal);
    v_tangent = vec4(mat3(model) * (mat3(skin) * tangent.xyz), tangent.w);
    v_uv0 = texcoord0;
    v_uv1 = texcoord1;
    v_color = color0 * instance_tint(record);
    v_world_pos = world.xyz;
}
@end

@include wgr_pbr.glsl


@fs fs
@include_block wgr_pbr_surface
@include_block wgr_pbr_main
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
@include_block wgr_pbr_color
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
