/* Toon shading (examples/shaders.c): the scene's lights in a few flat bands, plus a
 * rim of light around the silhouette. Built with tools/shaderpack.py (make example-shaders). */
@fs fs
@include_block wgr_surface
layout(binding=2) uniform params {
    vec4 color;      /* linear rgba, times the texture */
    float bands;     /* light levels */
    float rim;       /* rim light strength */
};
layout(binding=0) uniform texture2D base_tex;
layout(binding=0) uniform sampler base_smp;

void main() {
    vec4 albedo = texture(sampler2D(base_tex, base_smp), wgr_uv0);
    albedo.rgb = wgri_srgb_to_linear(albedo.rgb);
    albedo *= color * wgr_color;
    vec3 n = normalize(wgr_normal) * (gl_FrontFacing ? 1.0 : -1.0);
    vec3 light = wgr_ambient();
    for (int i = 0; i < 8; i++) {
        if (i >= wgr_light_count()) {
            break;
        }
        vec3 to_light;
        vec3 radiance = wgr_light(i, wgr_world_pos, to_light);
        float lambert = max(dot(n, to_light), 0.0);
        light += radiance * (floor(lambert * bands + 0.5) / max(bands, 1.0)) / 3.14159265;
    }
    vec3 v = normalize(wgr_camera_position() - wgr_world_pos);
    float edge = pow(1.0 - max(dot(n, v), 0.0), 3.0) * rim;
    wgr_output(albedo.rgb * light + vec3(edge), albedo.a);
}
@end
