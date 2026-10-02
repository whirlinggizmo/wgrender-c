#ifndef WGR_TEST_ASSETS_H
#define WGR_TEST_ASSETS_H

/* The asset layer, for tests that load files: resources load on create, through it.
 * Start it after the modules a test uses and stop it before them. */

/* The file store and the asset layer, with `root` as the asset root (a directory, e.g.
 * "examples/assets") and `workers` prepare threads (0: prepare on the main thread). */
void test_assets_start(int workers, const char *root);
void test_assets_stop(void);

/* Tick the asset layer until nothing is pending: the frames it took, or -1 after 2000. */
int test_assets_run(void);

#endif // WGR_TEST_ASSETS_H
