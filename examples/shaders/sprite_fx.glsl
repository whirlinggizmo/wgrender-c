/* Sprite effects (examples/shaders.c): an outline around the sprite's shape, and a
 * pulsing white flash (a hit, say). sk_sprite_color() is the sprite's own texture and
 * tint; the outline samples sk_sprite_tex itself, around each pixel. The same material
 * draws a 3D and a 2D sprite. Built with tools/shaderpack.py (make example-shaders). */
@fs fs
@include_block sk_surface
layout(binding=2) uniform params {
    vec4 outline_color;  /* linear rgba */
    float outline_width; /* texels */
    float flash;         /* 0..1: how white at the pulse's peak */
    float pulse_speed;   /* radians per second */
};

void main() {
    vec4 color = sk_sprite_color();
    /* the outline: a see-through pixel next to an opaque one */
    vec2 step = outline_width / vec2(textureSize(sampler2D(sk_sprite_tex, sk_sprite_smp), 0));
    float near = 0.0;
    for (int i = 0; i < 8; i++) {
        float a = 6.2831853 * float(i) / 8.0;
        near = max(near, texture(sampler2D(sk_sprite_tex, sk_sprite_smp), sk_uv0 + vec2(cos(a), sin(a)) * step).a);
    }
    float pulse = flash * (0.5 + 0.5 * sin(sk_time() * pulse_speed));
    vec3 rgb = mix(color.rgb, vec3(1.0), pulse);
    float alpha = color.a;
    if (color.a < 0.5 && near >= 0.5) {
        rgb = outline_color.rgb;
        alpha = outline_color.a;
    }
    sk_output(rgb, alpha);
}
@end
