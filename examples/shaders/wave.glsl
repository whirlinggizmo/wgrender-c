/* Water (examples/shaders.c): a vertex hook moves the surface along its normals in
 * travelling waves; the fragment shader colors it by height and reflects the scene's
 * environment, strongest at grazing angles. Built with tools/shaderpack.py
 * (make example-shaders). */
@block vertex
layout(binding=3) uniform wave_params {
    float amplitude;  /* object-space units */
    float frequency;  /* waves across one unit */
    float wave_speed; /* radians per second */
};
void wgr_vertex(inout vec3 position, inout vec3 normal) {
    float phase = (position.x + position.z) * frequency * 6.2831853 + wgr_time() * wave_speed;
    position += normal * sin(phase) * amplitude;
}
@end

@fs fs
@include_block wgr_surface
layout(binding=2) uniform params {
    vec4 low_color;  /* linear rgba */
    vec4 high_color;
    float roughness;    /* of the reflections, 0..1 */
    float reflectivity; /* facing the viewer (f0); rises to 1 at grazing angles */
};

void main() {
    vec3 n = normalize(wgr_normal);
    vec3 v = normalize(wgr_camera_position() - wgr_world_pos);
    vec3 light = wgr_ambient() + wgr_environment_diffuse(n);
    for (int i = 0; i < 8; i++) {
        if (i >= wgr_light_count()) {
            break;
        }
        vec3 to_light;
        light += wgr_light(i, wgr_world_pos, to_light) * max(dot(n, to_light), 0.0) / 3.14159265;
    }
    float height = clamp(wgr_world_pos.y * 0.5 + 0.5, 0.0, 1.0);
    vec4 color = mix(low_color, high_color, height);
    vec2 ab = wgr_environment_brdf(max(dot(n, v), 1e-4), roughness);
    vec3 reflection = wgr_environment_specular(n, v, roughness) * (vec3(reflectivity) * ab.x + ab.y);
    wgr_output(color.rgb * light * (1.0 - reflectivity) + reflection, color.a);
}
@end
