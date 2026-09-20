/* libwgrender custom shaders: what libwgrender gives a material's shader (docs/PLAN-materials.md,
 * "Custom shaders"). tools/shaderpack.py puts this file in front of yours, adds libwgrender's
 * vertex shaders (static and skinned models, and sprites) and compiles the result for
 * every backend into a .wgrshader file (wgr_shader_create). One shader draws models and
 * sprites (wgr_sprite3d_set_material, wgr_sprite2d_set_material) alike.
 *
 * Your file has a fragment shader named `fs`, and may have a vertex hook:
 *
 *     @block vertex                           (optional)
 *     void wgr_vertex(inout vec3 position, inout vec3 normal) { ... }   object space,
 *     @end                                    before skinning; wgr_time() is available.
 *                                             Models only: sprites don't run it.
 *
 *     @fs fs
 *     @include_block wgr_surface
 *     layout(binding=2) uniform params { vec4 tint; float speed; };  your parameters
 *     layout(binding=0) uniform texture2D noise_tex;                  your textures
 *     layout(binding=0) uniform sampler noise_smp;
 *     void main() {
 *         vec4 n = texture(sampler2D(noise_tex, noise_smp), wgr_uv0);
 *         wgr_output(tint.rgb * n.r, 1.0);
 *     }
 *     @end
 *
 * Textures 8 and up, and samplers 8 and up, are libwgrender's (the environment, the sprite's
 * texture, sprite data, skinned models' joints, a shadow map), as are uniform blocks 0,
 * 1 and 4.
 * Parameters: the members of uniform block binding 2 in the fragment shader and
 * binding 3 in the vertex hook, set by name with wgr_material_set_float / _vec2 /
 * _vec3 / _vec4 / _int / _color (float, vec2, vec3, vec4, int; not arrays or
 * matrices). Names must differ between the two blocks. Colors given as wgr_color_t
 * are converted to linear, like built-in materials. Textures: each texture2D (up to
 * 8, fragment shader, bindings 0-7) is set by its name with wgr_material_set_texture,
 * and sampled with the sampler it's paired with in texture(sampler2D(...)), set up
 * by wgr_material_set_texture_sampling. A texture that isn't set is white.
 *
 * Fragment inputs (wgr_surface), world space (a 2D sprite's: screen pixels):
 *   wgr_world_pos   vec3   the surface point
 *   wgr_normal      vec3   interpolated vertex normal (not normalized; facing out of
 *                         the front face; a sprite's faces its front)
 *   wgr_tangent     vec4   xyz tangent, w bitangent sign (glTF; a sprite's: its right)
 *   wgr_uv0, wgr_uv1 vec2   texture coordinate sets 0 and 1. A sprite's: its texture
 *                         region (uv0), and 0..1 across the quad whatever the region (uv1)
 *   wgr_color       vec4   vertex color, linear (white when the mesh has none); a
 *                         sprite's: its tint, linear
 *   wgr_tint        vec4   this placement's tint, linear rgba (white on sprites, whose
 *                         tint is in wgr_color). It comes from the instance record, so
 *                         models that differ only by tint still share one draw
 * and functions:
 *   wgr_time()             seconds since the program started
 *   wgr_camera_position()  world space
 *   wgr_ambient()          the scene's ambient light, linear rgb (0 outside a scene)
 *   wgr_light_count()      lights reaching this model (0..8)
 *   wgr_light(i, pos, out to_light)   light i's radiance arriving at world position
 *                         `pos` (linear rgb, attenuated), and the unit direction from
 *                         `pos` toward the light
 *   wgr_environment_intensity()   the scene's environment lighting strength (0: none;
 *                         the functions below then return black)
 *   wgr_environment_diffuse(n)    light from the environment arriving around normal n
 *                         (irradiance / pi, linear rgb): times the diffuse color
 *   wgr_environment_specular(n, v, roughness)   the environment reflected toward the
 *                         viewer (v: unit vector from the surface to the camera), blurred
 *                         for perceptual roughness 0..1 (linear rgb)
 *   wgr_environment_brdf(n_dot_v, roughness)   split-sum scale (x) and bias (y): specular
 *                         reflection = wgr_environment_specular(...) * (f0 * x + y)
 *   wgr_shadow(i, pos, n)  how much of light i reaches `pos` on a surface facing `n`:
 *                         1 in the open, 0 in shadow (docs/PLAN-shadows.md). Lights
 *                         that cast (up to four a scene) have a map; the rest are 1.
 *                         Multiply it into that light's contribution, as built-in
 *                         materials do. It already has the light's shadow strength in
 *                         it, and fades out where the light's map ends
 *   Built-in materials use exactly these (docs/PLAN-environment.md).
 *   wgr_sprite_color()     a sprite's texture at its region times its tint, linear rgba
 *                         (on a model: its vertex color). The texture itself is
 *                         wgr_sprite_tex / wgr_sprite_smp, to sample it yourself (an
 *                         outline reads the texels around), converting its rgb to linear
 *   wgri_srgb_to_linear(c), wgri_linear_to_srgb(c)
 *   wgr_output(color, alpha)   write the pixel: `color` is linear rgb. Applies the
 *                         model's tint, the material's (or sprite's) alpha mode, and
 *                         the scene's exposure and tone mapping (not on sprites), then
 *                         encodes sRGB. Call it once, at the end.
 * Lighting is linear and, like built-in materials, the framebuffer holds sRGB.
 * Names starting with wgr_ are libwgrender's.
 *
 * SCREEN EFFECTS (wgr_render_add_effect): a fragment shader that includes wgr_screen
 * instead of wgr_surface is a screen effect — it redraws the finished frame, pixel by
 * pixel, and has no surface, lighting or vertex hook. Its material can't be given to a
 * model or a sprite, and a surface shader's can't be added as an effect.
 *
 *     @fs fs
 *     @include_block wgr_screen
 *     layout(binding=2) uniform params { float strength; };
 *     void main() {
 *         vec2 d = wgr_screen_uv - 0.5;
 *         wgr_output(wgr_screen_color().rgb * (1.0 - dot(d, d) * strength), 1.0);
 *     }
 *     @end
 *
 * Parameters and textures work as above (block binding 2, textures 0-7). Inputs:
 *   wgr_screen_uv     vec2   this pixel, 0..1 from the top-left corner
 *   wgr_screen_color()       the frame here, linear rgba
 *   wgr_screen_color_at(uv)  the frame elsewhere (a blur, chromatic aberration)
 *   wgr_screen_size()  vec2  the frame in pixels, and wgr_screen_texel() = 1 / that
 *   wgr_time(), wgri_srgb_to_linear(c), wgri_linear_to_srgb(c)
 *   wgr_output(color, alpha)  write the pixel (linear rgb, encoded to sRGB). No tint,
 *                         alpha mode or tone mapping: an effect draws over the screen. */

@block wgr_color
vec3 wgri_srgb_to_linear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((max(c, vec3(0.04045)) + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 wgri_linear_to_srgb(vec3 c) {
    c = clamp(c, vec3(0.0), vec3(1.0));
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0031308)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}
@end

/* ----------------------------------------------------------------- vertex ---- */

@block wgr_vertex_default
void wgr_vertex(inout vec3 position, inout vec3 normal) {
}
@end

@block wgr_vs_outputs
layout(location=0) out vec3 wgr_world_pos;
layout(location=1) out vec3 wgr_normal;
layout(location=2) out vec4 wgr_tangent;
layout(location=3) out vec2 wgr_uv0;
layout(location=4) out vec2 wgr_uv1;
layout(location=5) out vec4 wgr_color;
layout(location=6) out float wgr_sprite_alpha; /* sprites: their alpha mode (0 for models) */
layout(location=7) out vec4 wgr_tint;          /* this placement's tint, linear rgba */
/* locations match libwgrender's vertex buffers */
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord0;
layout(location=3) in vec2 texcoord1;
layout(location=4) in vec4 tangent;
layout(location=5) in vec4 color0;
@end

@block wgr_vs_instance
/* The frame's per-placement records (src/wgr_model.c, docs/PLAN-instancing.md): where
 * this instance stands, how its normals turn, its tint, and which joint matrices are
 * its own. The draw says where its first record is; gl_InstanceIndex counts from
 * there, so models that agree on everything else go up as one draw. */
layout(binding=14) uniform texture2D wgr_instance_tex;
layout(binding=11) uniform sampler wgr_data_smp; /* nonfiltering; the joints share it */
@image_sample_type wgr_instance_tex unfilterable_float
@sampler_type wgr_data_smp nonfiltering

vec4 wgr_instance_texel(int record, int texel) {
    return texelFetch(sampler2D(wgr_instance_tex, wgr_data_smp),
                      ivec2((record % 128) * 8 + texel, record / 128), 0);
}
mat4 wgr_instance_model(int r) {
    vec4 r0 = wgr_instance_texel(r, 0), r1 = wgr_instance_texel(r, 1), r2 = wgr_instance_texel(r, 2);
    return mat4(vec4(r0.x, r1.x, r2.x, 0.0),
                vec4(r0.y, r1.y, r2.y, 0.0),
                vec4(r0.z, r1.z, r2.z, 0.0),
                vec4(r0.w, r1.w, r2.w, 1.0));
}
mat3 wgr_instance_normal_mat(int r) {
    vec4 n0 = wgr_instance_texel(r, 3), n1 = wgr_instance_texel(r, 4), n2 = wgr_instance_texel(r, 5);
    return mat3(vec3(n0.x, n1.x, n2.x), vec3(n0.y, n1.y, n2.y), vec3(n0.z, n1.z, n2.z));
}
vec4 wgr_instance_tint(int r) { return wgr_instance_texel(r, 6); }
int wgr_instance_joint_base(int r) { return int(wgr_instance_texel(r, 7).x); }
@end

@block wgr_vs_static_uniforms
@include_block wgr_vs_instance
layout(binding=0) uniform wgr_object {
    mat4 wgr_view_proj;
    vec4 wgr_object_time; /* x seconds, y this draw's first instance record */
};
float wgr_time() { return wgr_object_time.x; }
@end

@block wgr_vs_static_main
void main() {
    vec3 p = position;
    vec3 n = normal;
    int record = int(wgr_object_time.y) + gl_InstanceIndex;
    mat4 model = wgr_instance_model(record);
    wgr_vertex(p, n);
    vec4 world = model * vec4(p, 1.0);
    gl_Position = wgr_view_proj * world;
    wgr_world_pos = world.xyz;
    wgr_normal = wgr_instance_normal_mat(record) * n;
    wgr_tangent = vec4(mat3(model) * tangent.xyz, tangent.w);
    wgr_uv0 = texcoord0;
    wgr_uv1 = texcoord1;
    wgr_color = color0;
    wgr_tint = wgr_instance_tint(record);
    wgr_sprite_alpha = 0.0;
}
@end

@block wgr_vs_skinned_uniforms
@include_block wgr_vs_instance
layout(binding=0) uniform wgr_skinned_object {
    mat4 wgr_view_proj;
    vec4 wgr_object_time; /* x seconds, y this draw's first instance record */
};
/* the frame's joint matrices, 4 texels each, 256 a row (src/wgr_model.c); which ones are
   this instance's is in its record. Sampler slots stop at 11, so the two data textures
   share one: both are nearest, clamped, and never filtered. */
layout(binding=12) uniform texture2D wgr_joint_tex;
@image_sample_type wgr_joint_tex unfilterable_float
layout(location=6) in vec4 joints;
layout(location=7) in vec4 weights;
float wgr_time() { return wgr_object_time.x; }

mat4 wgr_joint_at(int base, int index) {
    int m = base + index;
    ivec2 t = ivec2((m % 256) * 4, m / 256);
    return mat4(texelFetch(sampler2D(wgr_joint_tex, wgr_data_smp), t, 0),
                texelFetch(sampler2D(wgr_joint_tex, wgr_data_smp), t + ivec2(1, 0), 0),
                texelFetch(sampler2D(wgr_joint_tex, wgr_data_smp), t + ivec2(2, 0), 0),
                texelFetch(sampler2D(wgr_joint_tex, wgr_data_smp), t + ivec2(3, 0), 0));
}
@end

@block wgr_vs_skinned_main
void main() {
    vec3 p = position;
    vec3 n = normal;
    int record = int(wgr_object_time.y) + gl_InstanceIndex;
    int joint_base = wgr_instance_joint_base(record);
    mat4 model = wgr_instance_model(record);
    wgr_vertex(p, n);
    mat4 skin = weights.x * wgr_joint_at(joint_base, int(joints.x))
              + weights.y * wgr_joint_at(joint_base, int(joints.y))
              + weights.z * wgr_joint_at(joint_base, int(joints.z))
              + weights.w * wgr_joint_at(joint_base, int(joints.w));
    vec4 sp = skin * vec4(p, 1.0);
    vec4 world = model * sp;
    gl_Position = wgr_view_proj * world;
    wgr_world_pos = world.xyz;
    wgr_normal = wgr_instance_normal_mat(record) * (mat3(skin) * n);
    wgr_tangent = vec4(mat3(model) * (mat3(skin) * tangent.xyz), tangent.w);
    wgr_uv0 = texcoord0;
    wgr_uv1 = texcoord1;
    wgr_color = color0;
    wgr_tint = wgr_instance_tint(record);
    wgr_sprite_alpha = 0.0;
}
@end

/* Sprites (src/wgr_sprite_batch.c): one quad per sprite, placed as libwgrender's own sprite
 * shader places it (src/shaders/wgr_sprite.glsl), from per-instance attributes, or from
 * a texture of sprite data where the backend can't draw from a base instance (WebGL2). */
@block wgr_vs_sprite_common
@include_block wgr_color
layout(binding=0) uniform wgr_sprite_view {
    mat4 wgr_view_proj;
    vec4 wgr_camera_right; /* xyz: camera-facing sprites' right */
    vec4 wgr_camera_up;
    vec4 wgr_upright;      /* xyz: upright sprites' right (their up is world Y) */
    vec4 wgr_sprite_time;  /* x seconds */
};
layout(location=0) out vec3 wgr_world_pos;
layout(location=1) out vec3 wgr_normal;
layout(location=2) out vec4 wgr_tangent;
layout(location=3) out vec2 wgr_uv0;
layout(location=4) out vec2 wgr_uv1;
layout(location=5) out vec4 wgr_color;
layout(location=6) out float wgr_sprite_alpha;
layout(location=7) out vec4 wgr_tint;
float wgr_time() { return wgr_sprite_time.x; }

/* corner: -0.5..0.5 each way (y up). pos: where the pivot goes, w facing. size: world
   size, then pivot (0..1, y down). source: its texture region. right, up: its own axes
   (facing 2+); up.w its alpha mode. tint: sRGB. */
void wgr_place(vec2 corner, vec4 pos, vec4 size, vec4 source, vec3 right_axis, vec4 up_axis, vec4 tint) {
    vec3 right = right_axis;
    vec3 up = up_axis.xyz;
    if (pos.w < 0.5) {
        right = wgr_camera_right.xyz;
        up = wgr_camera_up.xyz;
    } else if (pos.w < 1.5) {
        right = wgr_upright.xyz;
        up = vec3(0.0, 1.0, 0.0);
    }
    vec2 local = vec2((corner.x + 0.5 - size.z) * size.x, (corner.y - 0.5 + size.w) * size.y);
    vec3 world = pos.xyz + right * local.x + up * local.y;
    gl_Position = wgr_view_proj * vec4(world, 1.0);
    wgr_world_pos = world;
    wgr_normal = cross(right, up);
    wgr_tangent = vec4(right, 1.0);
    wgr_uv0 = vec2(mix(source.x, source.z, corner.x + 0.5), mix(source.y, source.w, 0.5 - corner.y));
    wgr_uv1 = vec2(corner.x + 0.5, 0.5 - corner.y);
    wgr_color = vec4(wgri_srgb_to_linear(tint.rgb), tint.a);
    wgr_tint = vec4(1.0); /* a sprite's tint is in wgr_color; wgr_output has none of its own */
    wgr_sprite_alpha = up_axis.w;
}
@end

@block wgr_vs_sprite_main
layout(location=0) in vec2 corner;
layout(location=1) in vec4 inst_pos;
layout(location=2) in vec4 inst_size;
layout(location=3) in vec4 inst_uv;
layout(location=4) in vec3 inst_right;
layout(location=5) in vec4 inst_up;
layout(location=6) in vec4 inst_color;
void main() {
    wgr_place(corner, inst_pos, inst_size, inst_uv, inst_right, inst_up, inst_color);
}
@end

@block wgr_vs_sprite_pulled_main
layout(binding=4) uniform wgr_sprite_batch {
    vec4 wgr_batch; /* x: the batch's first sprite */
};
layout(binding=11) uniform texture2D wgr_sprite_data;
layout(binding=11) uniform sampler wgr_sprite_data_smp;
@image_sample_type wgr_sprite_data unfilterable_float
@sampler_type wgr_sprite_data_smp nonfiltering
layout(location=0) in vec2 corner;
vec4 wgr_sprite_texel(int sprite, int k) {
    return texelFetch(sampler2D(wgr_sprite_data, wgr_sprite_data_smp), ivec2((sprite % 256) * 6 + k, sprite / 256), 0);
}
void main() {
    int sprite = int(wgr_batch.x) + gl_InstanceIndex;
    wgr_place(corner, wgr_sprite_texel(sprite, 0), wgr_sprite_texel(sprite, 1), wgr_sprite_texel(sprite, 2),
             wgr_sprite_texel(sprite, 3).xyz, wgr_sprite_texel(sprite, 4), wgr_sprite_texel(sprite, 5));
}
@end

/* Screen effects (wgr_render_add_effect): one triangle covering the screen, drawn over
 * the finished frame. No vertex buffer and no vertex hook: the fragment shader is the
 * whole effect. */
@block wgr_vs_screen_main
layout(location=0) out vec2 wgr_screen_uv;
void main() {
    /* (0,0), (2,0), (0,2): a triangle whose middle is the screen */
    vec2 c = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(c * 2.0 - 1.0, 0.0, 1.0);
    wgr_screen_uv = vec2(c.x, 1.0 - c.y); /* top-left origin, whatever the backend */
}
@end

/* --------------------------------------------------------------- fragment ---- */

@block wgr_screen
@include_block wgr_color
layout(binding=1) uniform wgr_screen_frame {
    vec4 wgr_screen_info; /* xy size in pixels, z seconds, w 1 when the frame is stored bottom-up */
};
layout(binding=8) uniform texture2D wgr_screen_tex;
layout(binding=8) uniform sampler wgr_screen_smp;
layout(location=0) in vec2 wgr_screen_uv;
out vec4 wgr_frag_color;

float wgr_time() { return wgr_screen_info.z; }
vec2 wgr_screen_size() { return wgr_screen_info.xy; }
vec2 wgr_screen_texel() { return 1.0 / max(wgr_screen_info.xy, vec2(1.0)); }

/* The frame at `uv` (top-left origin), linear rgba. */
vec4 wgr_screen_color_at(vec2 uv) {
    vec2 at = vec2(uv.x, wgr_screen_info.w > 0.5 ? 1.0 - uv.y : uv.y);
    vec4 c = texture(sampler2D(wgr_screen_tex, wgr_screen_smp), at);
    return vec4(wgri_srgb_to_linear(c.rgb), c.a);
}

vec4 wgr_screen_color() { return wgr_screen_color_at(wgr_screen_uv); }

void wgr_output(vec3 color, float alpha) {
    wgr_frag_color = vec4(wgri_linear_to_srgb(color), alpha);
}
@end

@block wgr_surface
@include_block wgr_color
layout(binding=1) uniform wgr_frame {
    vec4 wgr_camera_time;    /* xyz camera position, w seconds */
    vec4 wgr_ambient_count;  /* rgb ambient (linear), w number of lights */
    vec4 wgr_output_params;  /* x alpha cutoff (0 none), y tone mapping (0 none, 1 neutral, 2 ACES), z exposure scale */
    vec4 wgr_light_pos_range[8];  /* xyz position, w range (0 unlimited) */
    vec4 wgr_light_dir_type[8];   /* xyz direction the light travels, w type (0 directional, 1 point, 2 spot) */
    vec4 wgr_light_radiance[8];   /* rgb color x intensity, linear */
    vec4 wgr_light_spot[8];       /* x cos(inner angle), y cos(outer angle), z shadow slot (-1: none) */
    vec4 wgr_env;                 /* x intensity (0 none), y the cubemap's last mip, z/w cos/sin of its rotation */
    vec4 wgr_sh[9];               /* environment irradiance / pi, spherical harmonics (xyz) */
    /* shadows: up to four casting lights, a layer of one map each. A light's slot is
     * wgr_light_spot[i].z (-1: it casts none); the rest is per slot */
    mat4 wgr_shadow_mat[4];       /* world -> that light's clip space */
    vec4 wgr_shadow_params[4];    /* x 1/map size, y texel in world units, z/w bias constant, slope */
    vec4 wgr_shadow_tint[4];      /* rgb what a shadow keeps (linear), w bias texels -> depth */
    vec4 wgr_shadow_extra[4];     /* x strength */
    vec4 wgr_shadow_map;          /* x/y clip z -> stored depth, z 1 = stored top-down */
};
layout(binding=8) uniform textureCube wgr_env_tex;
layout(binding=8) uniform sampler wgr_env_smp; /* the BRDF table shares it: both are linear, clamped */
layout(binding=9) uniform texture2D wgr_brdf_tex;
layout(binding=13) uniform texture2DArray wgr_shadow_tex;
layout(binding=9) uniform sampler wgr_shadow_smp;
@image_sample_type wgr_shadow_tex depth
@sampler_type wgr_shadow_smp comparison
layout(location=0) in vec3 wgr_world_pos;
layout(location=1) in vec3 wgr_normal;
layout(location=2) in vec4 wgr_tangent;
layout(location=3) in vec2 wgr_uv0;
layout(location=4) in vec2 wgr_uv1;
layout(location=5) in vec4 wgr_color;
layout(location=6) in float wgr_sprite_alpha;
layout(location=7) in vec4 wgr_tint; /* the placement's tint, linear rgba */
layout(binding=10) uniform texture2D wgr_sprite_tex;
layout(binding=10) uniform sampler wgr_sprite_smp;
out vec4 wgr_frag_color;

vec4 wgr_sprite_color() {
    vec4 t = texture(sampler2D(wgr_sprite_tex, wgr_sprite_smp), wgr_uv0); /* white on models */
    return vec4(wgri_srgb_to_linear(t.rgb), t.a) * wgr_color;
}

float wgr_time() { return wgr_camera_time.w; }
vec3 wgr_camera_position() { return wgr_camera_time.xyz; }
vec3 wgr_ambient() { return wgr_ambient_count.rgb; }
int wgr_light_count() { return int(wgr_ambient_count.w + 0.5); }

/* The same falloff as built-in materials (src/wgr_light.c). */
vec3 wgr_light(int i, vec3 pos, out vec3 to_light) {
    int type = int(wgr_light_dir_type[i].w + 0.5);
    if (type == 0) {
        to_light = -normalize(wgr_light_dir_type[i].xyz);
        return wgr_light_radiance[i].rgb;
    }
    vec3 d = pos - wgr_light_pos_range[i].xyz;
    float dist = length(d);
    vec3 dir = dist > 1e-6 ? d / dist : vec3(0.0, -1.0, 0.0);
    to_light = -dir;
    float falloff = 1.0 / max(dist * dist, 0.01);
    float range = wgr_light_pos_range[i].w;
    if (range > 0.0) {
        float r = dist / range;
        float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
        falloff *= w * w;
    }
    if (type == 2) {
        float cos_angle = dot(dir, normalize(wgr_light_dir_type[i].xyz));
        float cos_inner = wgr_light_spot[i].x;
        float cos_outer = wgr_light_spot[i].y;
        falloff *= cos_inner - cos_outer <= 1e-6 ? (cos_angle >= cos_outer ? 1.0 : 0.0)
                                                 : smoothstep(cos_outer, cos_inner, cos_angle);
    }
    return wgr_light_radiance[i].rgb * falloff;
}

float wgr_environment_intensity() { return wgr_env.x; }

/* A world direction in the environment's frame (rotated by -rotation around +y). */
vec3 wgr_environment_dir(vec3 dir) {
    return vec3(wgr_env.z * dir.x - wgr_env.w * dir.z, dir.y, wgr_env.w * dir.x + wgr_env.z * dir.z);
}

vec3 wgr_environment_diffuse(vec3 n) {
    vec3 d = wgr_environment_dir(n);
    vec3 irradiance = wgr_sh[0].xyz * 0.282095
                    + wgr_sh[1].xyz * (0.488603 * d.y)
                    + wgr_sh[2].xyz * (0.488603 * d.z)
                    + wgr_sh[3].xyz * (0.488603 * d.x)
                    + wgr_sh[4].xyz * (1.092548 * d.x * d.y)
                    + wgr_sh[5].xyz * (1.092548 * d.y * d.z)
                    + wgr_sh[6].xyz * (0.315392 * (3.0 * d.z * d.z - 1.0))
                    + wgr_sh[7].xyz * (1.092548 * d.x * d.z)
                    + wgr_sh[8].xyz * (0.546274 * (d.x * d.x - d.y * d.y));
    return max(irradiance, vec3(0.0)) * wgr_env.x;
}

vec3 wgr_environment_specular(vec3 n, vec3 v, float roughness) {
    vec3 r = wgr_environment_dir(reflect(-v, n));
    return textureLod(samplerCube(wgr_env_tex, wgr_env_smp), r, clamp(roughness, 0.0, 1.0) * wgr_env.y).rgb * wgr_env.x;
}

/* How much of light `i` reaches `pos`: 1 in the open, 0 in shadow, in between across
 * the edge. Only the scene's casting light is shadowed; every other light returns 1,
 * as does any light when nothing casts. `n_dot_l` tilts the depth bias for surfaces
 * that face the light edge-on (pass max(dot(n, to_light), 0)). */
float wgr_shadow(int i, vec3 pos, vec3 n) {
    int slot = int(wgr_light_spot[i].z);
    if (slot < 0) {
        return 1.0; /* this light casts no shadow */
    }
    vec3 normal = normalize(n);
    vec3 to_light = -normalize(wgr_light_dir_type[i].xyz);
    float slant = 1.0 - clamp(dot(normal, to_light), 0.0, 1.0);
    vec4 clip = wgr_shadow_mat[slot] * vec4(pos + normal * (wgr_shadow_params[slot].y * (1.0 + 2.0 * slant)), 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 1e-6) * sign(clip.w);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (wgr_shadow_map.z > 0.5) {
        uv.y = 1.0 - uv.y;
    }
    float bias = (wgr_shadow_params[slot].z + wgr_shadow_params[slot].w * slant) * wgr_shadow_tint[slot].w;
    float depth = ndc.z * wgr_shadow_map.x + wgr_shadow_map.y - bias;
    /* a weight, not an early return: the comparison below runs for every pixel */
    vec2 to_edge = 1.0 - abs(ndc.xy);
    float inside = step(0.0, min(to_edge.x, to_edge.y)) * step(depth, 1.0) * step(0.0, clip.w);
    float edge = clamp(min(to_edge.x, to_edge.y) / 0.1, 0.0, 1.0) * inside;
    float texel = wgr_shadow_params[slot].x;
    vec2 at = clamp(uv, vec2(texel), vec2(1.0 - texel));
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += texture(sampler2DArrayShadow(wgr_shadow_tex, wgr_shadow_smp),
                           vec4(at + vec2(float(x), float(y)) * texel, float(slot), clamp(depth, 0.0, 1.0)));
        }
    }
    return mix(1.0, lit / 9.0, edge * wgr_shadow_extra[slot].x);
}

vec2 wgr_environment_brdf(float n_dot_v, float roughness) {
    return texture(sampler2D(wgr_brdf_tex, wgr_env_smp), vec2(clamp(n_dot_v, 0.0, 1.0), clamp(roughness, 0.0, 1.0))).rg;
}

vec3 wgr_tonemap_neutral(vec3 color) {
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

vec3 wgr_tonemap_aces(vec3 color) {
    color *= 0.6;
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

void wgr_output(vec3 color, float alpha) {
    color *= wgr_tint.rgb;
    alpha *= wgr_tint.a;
    /* the material's cutoff, or a sprite's (its alpha mode: > 0 a cutoff, < 0 opaque) */
    float cutoff = max(wgr_output_params.x, max(wgr_sprite_alpha, 0.0));
    if (alpha < cutoff) {
        discard;
    }
    if (wgr_sprite_alpha != 0.0) {
        alpha = 1.0; /* opaque and masked sprites */
    }
    color *= wgr_output_params.z;
    int mode = int(wgr_output_params.y + 0.5);
    if (mode == 1) {
        color = wgr_tonemap_neutral(color);
    } else if (mode == 2) {
        color = wgr_tonemap_aces(color);
    }
    wgr_frag_color = vec4(wgri_linear_to_srgb(color), alpha);
}
@end
