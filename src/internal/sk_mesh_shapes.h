#ifndef SK_INTERNAL_MESH_SHAPES_H
#define SK_INTERNAL_MESH_SHAPES_H

#include <stdbool.h>
#include <stdint.h>

/* Geometry of the generated meshes (sk_mesh_create_plane, ...): positions, unit
 * normals and texture coordinates per vertex, and triangles front-facing
 * counterclockwise (glTF's convention, y up). Centered on the origin. Pure: the model
 * module makes meshes of them. Texture coordinates start at the top-left (v down), as
 * glTF's do. */

typedef struct {
    float *positions; /* 3 per vertex */
    float *normals;   /* 3 per vertex */
    float *uvs;       /* 2 per vertex */
    uint32_t *indices;
    int vertex_count;
    int index_count;
} sk_mesh_shape_t;

/* Parameter limits: counts outside them are clamped (sizes aren't: <= 0 fails). */
#define SK_MESH_MAX_SUBDIVISIONS 256
#define SK_MESH_MIN_RINGS 2
#define SK_MESH_MAX_RINGS 256
#define SK_MESH_MIN_SEGMENTS 3
#define SK_MESH_MAX_SEGMENTS 512

/* false (and *out empty) for sizes <= 0 or out of memory. */
bool sk_mesh_shape_plane(float width, float length, int subdivisions, sk_mesh_shape_t *out);
bool sk_mesh_shape_cube(float width, float height, float length, sk_mesh_shape_t *out);
bool sk_mesh_shape_sphere(float radius, int rings, int segments, sk_mesh_shape_t *out);
bool sk_mesh_shape_cylinder(float radius, float height, int segments, sk_mesh_shape_t *out);
bool sk_mesh_shape_cone(float radius, float height, int segments, sk_mesh_shape_t *out);
bool sk_mesh_shape_capsule(float radius, float height, int rings, int segments, sk_mesh_shape_t *out);
bool sk_mesh_shape_torus(float radius, float thickness, int rings, int segments, sk_mesh_shape_t *out);
void sk_mesh_shape_free(sk_mesh_shape_t *shape);

#endif // SK_INTERNAL_MESH_SHAPES_H
