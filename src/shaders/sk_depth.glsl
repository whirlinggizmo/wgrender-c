/* sk_shadow depth pass — the casters as a light sees them (docs/PLAN-shadows.md).
 *
 * Only depth is written: the pass has no color attachment, so the fragment shader has
 * nothing to say unless the material is alpha-tested, where a cut-out texel must not
 * cast. Static and skinned stages mirror sk_model.glsl's, minus everything shading
 * needs; the skinned one reads the same joint texture the model shader does.
 *
 * Uniform blocks (std140, mirrored in src/sk_shadow.c):
 *   vs_depth_params      = light_mvp
 *   vs_depth_skin_params = light_mvp, skin_base (x: this model's first joint matrix)
 *   fs_depth_params      = x alpha cutoff (0 = no alpha test)
 *
 * Regen: `make shaders`. */
@ctype mat4 sk_mat4_t

@block depth_outputs
out vec2 v_uv0;
@end

@vs vs_depth_static
layout(binding=0) uniform vs_depth_params {
    mat4 light_mvp;
};
in vec3 position;
in vec2 texcoord0;
@include_block depth_outputs
void main() {
    gl_Position = light_mvp * vec4(position, 1.0);
    v_uv0 = texcoord0;
}
@end

@vs vs_depth_skinned
layout(binding=0) uniform vs_depth_skin_params {
    mat4 light_mvp;
    vec4 skin_base;
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
in vec2 texcoord0;
in vec4 joints;
in vec4 weights;
@include_block depth_outputs
void main() {
    mat4 skin = weights.x * joint_at(int(joints.x))
              + weights.y * joint_at(int(joints.y))
              + weights.z * joint_at(int(joints.z))
              + weights.w * joint_at(int(joints.w));
    gl_Position = light_mvp * (skin * vec4(position, 1.0));
    v_uv0 = texcoord0;
}
@end

@fs fs_depth
layout(binding=1) uniform fs_depth_params {
    vec4 cutoff; /* x: alpha cutoff, 0 = no alpha test */
};
layout(binding=0) uniform texture2D base_color_tex;
layout(binding=0) uniform sampler base_color_smp;
in vec2 v_uv0;
void main() {
    /* a cut-out leaf or fence casts the shape it draws, not its quad */
    if (cutoff.x > 0.0 && texture(sampler2D(base_color_tex, base_color_smp), v_uv0).a < cutoff.x) {
        discard;
    }
}
@end

@program depth_static vs_depth_static fs_depth
@program depth_skinned vs_depth_skinned fs_depth
