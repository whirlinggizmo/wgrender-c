/* Toon shading (examples/shaders.c): the scene's lights in a few flat bands, plus a
 * rim of light around the silhouette. Built with tools/shaderpack.py (make example-shaders). */
@fs fs
@include_block sk_surface
layout(binding=2) uniform params {
    vec4 color;      /* linear rgba, times the texture */
    float bands;     /* light levels */
    float rim;       /* rim light strength */
};
layout(binding=0) uniform texture2D base_tex;
layout(binding=0) uniform sampler base_smp;

void main() {
    vec4 albedo = texture(sampler2D(base_tex, base_smp), sk_uv0);
    albedo.rgb = sk_srgb_to_linear(albedo.rgb);
    albedo *= color * sk_color;
    vec3 n = normalize(sk_normal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 light = sk_ambient();
    for (int i = 0; i < 8; i++) {
        if (i >= sk_light_count()) {
            break;
        }
        vec3 to_light;
        vec3 radiance = sk_light(i, sk_world_pos, to_light);
        float lambert = max(dot(n, to_light), 0.0);
        light += radiance * (floor(lambert * bands + 0.5) / max(bands, 1.0)) / 3.14159265;
    }
    vec3 v = normalize(sk_camera_position() - sk_world_pos);
    float edge = pow(1.0 - max(dot(n, v), 0.0), 3.0) * rim;
    sk_output(albedo.rgb * light + vec3(edge), albedo.a);
}
@end
