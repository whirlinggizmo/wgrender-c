#include "test_assets.h"

#include "internal/wgr_asset_internal.h"
#include "internal/wgr_fs_internal.h"
#include "internal/wgr_internal_internal.h"
#include "wgr_asset.h"
#include "test_os.h"

#include <stdio.h>

char test_assets_path[512];

void test_assets_start(int workers, const char *root)
{
    wgri_fs_init(NULL);
    wgri_asset_set_worker_count(workers);
    wgri_asset_init();
    wgr_asset_set_host(root);
}

void test_assets_stop(void)
{
    wgri_asset_deinit();
    wgri_asset_set_worker_count(-1);
    wgri_fs_deinit();
}

int test_assets_run(void)
{
    for (int frame = 1; frame <= 2000; frame++) {
        wgri_asset_tick();
        if (wgri_asset_pending_count() == 0) return frame;
        if (wgri_asset_get_worker_count() > 0) {
            test_sleep_ms(1);
        }
    }
    return -1;
}

wgr_asset_task_status_t test_assets_ensure(const char *path, const char *fetch_url, unsigned int flags, int frames)
{
    const wgr_handle_t task = wgr_asset_ensure(path, fetch_url, flags);
    wgr_asset_task_status_t status;

    test_assets_path[0] = '\0';
    if (task == 0) {
        return WGR_ASSET_TASK_NONE;
    }
    for (int frame = 0; frame < frames && wgr_asset_task_get_status(task) == WGR_ASSET_TASK_PENDING; frame++) {
        wgri_asset_tick();
    }
    status = wgr_asset_task_get_status(task);
    snprintf(test_assets_path, sizeof(test_assets_path), "%s", wgr_asset_task_get_path(task));
    wgr_asset_task_destroy(task);
    return status;
}
