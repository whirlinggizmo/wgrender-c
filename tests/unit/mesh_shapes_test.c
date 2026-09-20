/* Generated mesh geometry (internal/wgr_mesh_shapes.h): counts, bounds, unit normals
 * that point out of the shape, every triangle facing its normals' way, texture
 * coordinates in 0..1, and refusal of sizes <= 0. */
#include <math.h>

#include "internal/wgr_mesh_shapes_internal.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

/* Unit normals; texture coordinates in 0..1; indices in range; each triangle wound
 * counterclockwise seen from the side its normals point to. `outward`: the normal
 * points away from the center point `center(p)` gives for a position (the origin for
 * convex shapes). */
static void check_shape(const wgr_mesh_shape_t *s, void (*center)(const float *p, float *c))
{
    int bad_normals = 0, bad_uvs = 0, bad_outward = 0, bad_winding = 0, bad_indices = 0;
    for (int i = 0; i < s->vertex_count; i++) {
        const float *n = &s->normals[i * 3], *p = &s->positions[i * 3], *uv = &s->uvs[i * 2];
        float c[3] = {0, 0, 0};
        if (fabsf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] - 1.0f) > EPS) bad_normals++;
        if (uv[0] < -EPS || uv[0] > 1 + EPS || uv[1] < -EPS || uv[1] > 1 + EPS) bad_uvs++;
        if (center != NULL) center(p, c);
        if (n[0] * (p[0] - c[0]) + n[1] * (p[1] - c[1]) + n[2] * (p[2] - c[2]) < -EPS) bad_outward++;
    }
    for (int t = 0; t + 2 < s->index_count; t += 3) {
        const uint32_t *ix = &s->indices[t];
        if (ix[0] >= (uint32_t)s->vertex_count || ix[1] >= (uint32_t)s->vertex_count ||
            ix[2] >= (uint32_t)s->vertex_count) {
            bad_indices++;
            continue;
        }
        const float *a = &s->positions[ix[0] * 3], *b = &s->positions[ix[1] * 3], *c = &s->positions[ix[2] * 3];
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float x[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        float facing = 0;
        for (int k = 0; k < 3; k++) {
            facing += x[k] * (s->normals[ix[0] * 3 + k] + s->normals[ix[1] * 3 + k] + s->normals[ix[2] * 3 + k]);
        }
        if (facing <= 0) bad_winding++;
    }
    CHECK(bad_normals == 0);
    CHECK(bad_uvs == 0);
    CHECK(bad_outward == 0);
    CHECK(bad_winding == 0);
    CHECK(bad_indices == 0);
}

static void bounds(const wgr_mesh_shape_t *s, float min[3], float max[3])
{
    for (int k = 0; k < 3; k++) min[k] = 1e30f, max[k] = -1e30f;
    for (int i = 0; i < s->vertex_count; i++) {
        for (int k = 0; k < 3; k++) {
            min[k] = fminf(min[k], s->positions[i * 3 + k]);
            max[k] = fmaxf(max[k], s->positions[i * 3 + k]);
        }
    }
}

/* the torus's normals point away from the circle through the middle of its tube */
static void tube_center(const float *p, float *c)
{
    const float d = sqrtf(p[0] * p[0] + p[2] * p[2]);
    c[0] = p[0] / d * 2.0f; /* radius 2 below */
    c[1] = 0;
    c[2] = p[2] / d * 2.0f;
}

/* the capsule's normals point away from the segment between its end centers */
static void capsule_axis(const float *p, float *c)
{
    c[0] = 0;
    c[1] = fmaxf(-0.5f, fminf(0.5f, p[1])); /* radius 0.5, height 2 below: centers at +-0.5 */
    c[2] = 0;
}

void test_mesh_shapes(void)
{
    wgr_mesh_shape_t s;
    float lo[3], hi[3];

    CHECK(wgr_mesh_shape_plane(4, 2, 0, &s));
    CHECK(s.vertex_count == 4 && s.index_count == 6);
    check_shape(&s, NULL);
    bounds(&s, lo, hi);
    CHECK_NEAR(lo[0], -2, EPS);
    CHECK_NEAR(hi[2], 1, EPS);
    CHECK_NEAR(hi[1], 0, EPS);
    CHECK(s.normals[1] == 1.0f); /* facing +y */
    wgr_mesh_shape_free(&s);
    CHECK(wgr_mesh_shape_plane(4, 2, 3, &s));
    CHECK(s.vertex_count == 25 && s.index_count == 4 * 4 * 6);
    wgr_mesh_shape_free(&s);

    CHECK(wgr_mesh_shape_cube(2, 4, 6, &s));
    CHECK(s.vertex_count == 24 && s.index_count == 36);
    check_shape(&s, NULL);
    bounds(&s, lo, hi);
    CHECK_NEAR(lo[0], -1, EPS);
    CHECK_NEAR(hi[1], 2, EPS);
    CHECK_NEAR(hi[2], 3, EPS);
    wgr_mesh_shape_free(&s);

    CHECK(wgr_mesh_shape_sphere(1.5f, 8, 16, &s));
    CHECK(s.vertex_count == 9 * 17);
    CHECK(s.index_count == 6 * 16 * (8 - 1)); /* one triangle of each quad at the poles has no area */
    check_shape(&s, NULL);
    bounds(&s, lo, hi);
    CHECK_NEAR(hi[1], 1.5f, EPS);
    CHECK_NEAR(lo[1], -1.5f, EPS);
    wgr_mesh_shape_free(&s);

    CHECK(wgr_mesh_shape_cylinder(1, 2, 12, &s));
    CHECK(s.index_count == 12 * 6 + 2 * 12 * 3); /* side, two caps */
    check_shape(&s, NULL);
    bounds(&s, lo, hi);
    CHECK_NEAR(hi[1], 1, EPS);
    CHECK_NEAR(lo[1], -1, EPS);
    wgr_mesh_shape_free(&s);

    CHECK(wgr_mesh_shape_cone(1, 2, 12, &s));
    CHECK(s.index_count == 12 * 3 + 12 * 3); /* side, base */
    check_shape(&s, NULL);
    bounds(&s, lo, hi);
    CHECK_NEAR(hi[1], 1, EPS);
    wgr_mesh_shape_free(&s);

    CHECK(wgr_mesh_shape_capsule(0.5f, 2, 8, 12, &s));
    check_shape(&s, capsule_axis);
    bounds(&s, lo, hi);
    CHECK_NEAR(hi[1], 1, EPS); /* height is end to end */
    CHECK_NEAR(lo[1], -1, EPS);
    CHECK_NEAR(hi[0], 0.5f, 1e-3f);
    wgr_mesh_shape_free(&s);
    CHECK(wgr_mesh_shape_capsule(0.5f, 0.2f, 8, 12, &s)); /* shorter than its ends: a sphere */
    bounds(&s, lo, hi);
    CHECK_NEAR(hi[1], 0.5f, EPS);
    wgr_mesh_shape_free(&s);

    CHECK(wgr_mesh_shape_torus(2, 0.5f, 24, 12, &s));
    CHECK(s.vertex_count == 25 * 13 && s.index_count == 24 * 12 * 6);
    check_shape(&s, tube_center);
    bounds(&s, lo, hi);
    CHECK_NEAR(hi[0], 2.5f, EPS);
    CHECK_NEAR(hi[1], 0.5f, EPS);
    wgr_mesh_shape_free(&s);

    /* counts are clamped; sizes aren't */
    CHECK(wgr_mesh_shape_sphere(1, 0, 0, &s));
    CHECK(s.vertex_count == (WGR_MESH_MIN_RINGS + 1) * (WGR_MESH_MIN_SEGMENTS + 1));
    wgr_mesh_shape_free(&s);
    CHECK(!wgr_mesh_shape_plane(0, 1, 0, &s) && s.positions == NULL);
    CHECK(!wgr_mesh_shape_cube(1, -1, 1, &s));
    CHECK(!wgr_mesh_shape_sphere(NAN, 8, 8, &s));
    CHECK(!wgr_mesh_shape_torus(1, 0, 8, 8, &s));
}
