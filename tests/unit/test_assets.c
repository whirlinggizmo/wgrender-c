#include "test_assets.h"

#include "internal/wgr_asset_internal.h"
#include "internal/wgr_fs_internal.h"
#include "internal/wgr_internal_internal.h"
#include "wgr_asset.h"
#include "test_os.h"

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
