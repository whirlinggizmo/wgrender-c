#ifndef WGR_TEST_ASSETS_H
#define WGR_TEST_ASSETS_H

#include "wgr_asset.h"

/* The asset layer, for tests that load files: resources load on create, through it.
 * Start it after the modules a test uses and stop it before them. */

/* The file store and the asset layer, with `root` as the asset root (a directory, e.g.
 * "examples/assets") and `workers` prepare threads (0: prepare on the main thread). */
void test_assets_start(int workers, const char *root);
void test_assets_stop(void);

/* Tick the asset layer until nothing is pending: the frames it took, or -1 after 2000. */
int test_assets_run(void);

/* A test's downloader: what a program does with each request it takes
 * (wgr_asset_fetch_next), here called with the request's URL and destination. */
typedef void (*test_fetch_fn)(wgr_handle_t request, const char *url, const char *dest_path, void *user);

/* Turn fetching on with `fn` as the downloader, or off with NULL (wgr_asset_set_fetching). */
bool test_assets_set_fetcher(test_fetch_fn fn, void *user);

/* One frame of the asset layer: a tick, then every request it made handed to the
 * fetcher, as a program polls once a frame. */
void test_assets_tick(void);

/* Ensure a file (wgr_asset_ensure), tick the asset layer until the task finishes or
 * `frames` have passed, and destroy it: its status (PENDING if it didn't finish, NONE
 * if it was refused), with its path in test_assets_path ("" unless DONE). */
wgr_asset_task_status_t test_assets_ensure(const char *path, const char *fetch_url, unsigned int flags, int frames);
extern char test_assets_path[512];

#endif // WGR_TEST_ASSETS_H
