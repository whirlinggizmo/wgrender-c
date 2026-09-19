/* Waves (examples/shaders.c): a vertex hook moves the surface along its normals in
 * travelling waves; the fragment shader colors it by height. Built with
 * tools/shaderpack.py (make example-shaders). */
@block vertex
layout(binding=3) uniform wave_params {
    float amplitude;  /* object-space units */
    float frequency;  /* waves across one unit */
    float wave_speed; /* radians per second */
};
void sk_vertex(inout vec3 position, inout vec3 normal) {
    float phase = (position.x + position.z) * frequency * 6.2831853 + sk_time() * wave_speed;
    position += normal * sin(phase) * amplitude;
}
@end

@fs fs
@include_block sk_surface
layout(binding=2) uniform params {
    vec4 low_color;  /* linear rgba */
    vec4 high_color;
};

void main() {
    vec3 n = normalize(sk_normal);
    vec3 light = sk_ambient();
    for (int i = 0; i < 8; i++) {
        if (i >= sk_light_count()) {
            break;
        }
        vec3 to_light;
        light += sk_light(i, sk_world_pos, to_light) * max(dot(n, to_light), 0.0) / 3.14159265;
    }
    float height = clamp(sk_world_pos.y * 0.5 + 0.5, 0.0, 1.0);
    vec4 color = mix(low_color, high_color, height);
    sk_output(color.rgb * light, color.a);
}
@end
