#include "internal/wgr_sprite2d_internal.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-3f
#define HALF_PI 1.5707963f

static wgri_sprite2d_placement_t placement(float rotation, float scale_x, float scale_y, float pivot_x, float pivot_y)
{
    /* 40 x 20 sprite with its pivot at (100, 50) */
    return (wgri_sprite2d_placement_t){.x = 100, .y = 50, .width = 40, .height = 20, .scale_x = scale_x,
                                     .scale_y = scale_y, .pivot_x = pivot_x, .pivot_y = pivot_y, .rotation = rotation};
}

void test_sprite2d_corners(void)
{
    float c[8];

    /* centered pivot, no rotation: corners around (100, 50) */
    wgri_sprite2d_placement_t p = placement(0, 1, 1, 0.5f, 0.5f);
    wgri_sprite2d_corners(&p, c);
    CHECK_NEAR(c[0], 80, EPS);  CHECK_NEAR(c[1], 40, EPS);  /* top-left */
    CHECK_NEAR(c[4], 120, EPS); CHECK_NEAR(c[5], 60, EPS);  /* bottom-right */

    /* top-left pivot: the position is the top-left corner */
    p = placement(0, 1, 1, 0, 0);
    wgri_sprite2d_corners(&p, c);
    CHECK_NEAR(c[0], 100, EPS); CHECK_NEAR(c[1], 50, EPS);
    CHECK_NEAR(c[4], 140, EPS); CHECK_NEAR(c[5], 70, EPS);

    /* +90 degrees turns clockwise on a y-down screen: the texture's top-right
     * corner (right of the pivot) moves below the pivot */
    p = placement(HALF_PI, 1, 1, 0, 0);
    wgri_sprite2d_corners(&p, c);
    CHECK_NEAR(c[2], 100, EPS); CHECK_NEAR(c[3], 90, EPS); /* top-right: (140,50) -> (100,90) */

    /* negative x scale mirrors around the pivot */
    p = placement(0, -1, 1, 0, 0);
    wgri_sprite2d_corners(&p, c);
    CHECK_NEAR(c[0], 100, EPS); /* top-left stays at the pivot */
    CHECK_NEAR(c[2], 60, EPS);  /* top-right is now to the left */
}

void test_sprite2d_screen_to_unit(void)
{
    float u = -1, v = -1;

    wgri_sprite2d_placement_t p = placement(0, 1, 1, 0.5f, 0.5f);
    CHECK(wgri_sprite2d_screen_to_unit(&p, 100, 50, &u, &v)); /* center */
    CHECK_NEAR(u, 0.5f, EPS); CHECK_NEAR(v, 0.5f, EPS);
    CHECK(wgri_sprite2d_screen_to_unit(&p, 81, 41, &u, &v));  /* near top-left */
    CHECK_NEAR(u, 0.025f, EPS); CHECK_NEAR(v, 0.05f, EPS);
    CHECK(!wgri_sprite2d_screen_to_unit(&p, 79, 50, &u, &v)); /* just left of it */
    CHECK(!wgri_sprite2d_screen_to_unit(&p, 100, 61, &u, &v)); /* just below */

    /* scaled 2x: the same point is closer to the center in unit space */
    p = placement(0, 2, 2, 0.5f, 0.5f);
    CHECK(wgri_sprite2d_screen_to_unit(&p, 79, 50, &u, &v));
    CHECK_NEAR(u, 0.2375f, EPS); /* spans x 60..140: (79 - 60) / 80 */

    /* rotated 90 degrees: a point below the pivot maps to the texture's right side */
    p = placement(HALF_PI, 1, 1, 0, 0);
    CHECK(wgri_sprite2d_screen_to_unit(&p, 99, 80, &u, &v));
    CHECK_NEAR(u, 0.75f, EPS);  /* 30 of 40 along the texture's x */
    CHECK_NEAR(v, 0.05f, EPS);  /* 1 of 20 along its y */
    CHECK(!wgri_sprite2d_screen_to_unit(&p, 130, 55, &u, &v)); /* where it was before rotating */

    /* flipped: u runs the other way, so texture-based alpha tests stay correct */
    p = placement(0, -1, 1, 0.5f, 0.5f);
    CHECK(wgri_sprite2d_screen_to_unit(&p, 81, 50, &u, &v));
    CHECK_NEAR(u, 0.975f, EPS);

    /* zero-size sprites never hit */
    p = placement(0, 0, 1, 0.5f, 0.5f);
    CHECK(!wgri_sprite2d_screen_to_unit(&p, 100, 50, &u, &v));
}
