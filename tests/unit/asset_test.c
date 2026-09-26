#include <stdio.h>
#include <string.h>

#include "internal/wgr_fs_internal.h"
#include "internal/wgr_internal_internal.h"
#include "wgr_asset.h"

#include "internal/wgr_asset_internal.h"
#include "test.h"
#include "tests.h"

static void check_join(const char *base, const char *uri, const char *expected)
{
    char out[256];
    const bool ok = wgri_asset_join_relative(base, uri, out, sizeof(out));
    CHECK(ok == (expected != NULL));
    if (ok && expected != NULL && strcmp(out, expected) != 0) {
        fprintf(stderr, "    join(%s, %s): got %s, expected %s\n", base, uri, out, expected);
        wgr_test_failures++;
    }
}

/* How long a response may be used without asking (Cache-Control, Age): the cache's
 * whole idea of freshness (wgr_asset.h, WGR_ASSET_CACHE_REVALIDATE). */
void test_asset_freshness(void)
{
    const double now = 1790000000.0;
    const double year = 365.0 * 24.0 * 3600.0;

    /* nothing said, nothing fresh: every visit asks */
    CHECK(wgri_asset_fresh_until(NULL, NULL, now) == 0.0);
    CHECK(wgri_asset_fresh_until("", "", now) == 0.0);
    CHECK(wgri_asset_fresh_until("public", NULL, now) == 0.0);

    /* max-age from now, less what a shared cache already held it for */
    CHECK(wgri_asset_fresh_until("max-age=600", NULL, now) == now + 600.0);
    CHECK(wgri_asset_fresh_until("public, max-age=600", "100", now) == now + 500.0);
    CHECK(wgri_asset_fresh_until("max-age=600", "600", now) == 0.0);
    CHECK(wgri_asset_fresh_until("max-age=600", "9000", now) == 0.0);
    CHECK(wgri_asset_fresh_until("max-age=0", NULL, now) == 0.0);
    CHECK(wgri_asset_fresh_until("Max-Age=60", NULL, now) == now + 60.0); /* directives ignore case */
    CHECK(wgri_asset_fresh_until("max-age=600", "junk", now) == now + 600.0);

    /* a max-age that isn't a number is none */
    CHECK(wgri_asset_fresh_until("max-age=soon", NULL, now) == 0.0);
    CHECK(wgri_asset_fresh_until("max-age=-5", NULL, now) == 0.0);
    CHECK(wgri_asset_fresh_until("max-age=\"600\"", NULL, now) == 0.0);

    /* immutable: max-age still says how long; without one, a year */
    CHECK(wgri_asset_fresh_until("public, max-age=31536000, immutable", NULL, now) == now + 31536000.0);
    CHECK(wgri_asset_fresh_until("immutable", NULL, now) == now + year);

    /* no-cache and no-store win wherever they are, field-specific no-cache included */
    CHECK(wgri_asset_fresh_until("max-age=600, no-cache", NULL, now) == 0.0);
    CHECK(wgri_asset_fresh_until("no-store, max-age=600", NULL, now) == 0.0);
    CHECK(wgri_asset_fresh_until("immutable,no-cache=\"Set-Cookie\"", NULL, now) == 0.0);
    CHECK(wgri_asset_fresh_until("no-cacheable, max-age=60", NULL, now) == now + 60.0); /* not no-cache */
    CHECK(wgri_asset_fresh_until("  max-age=60  ,  must-revalidate ", NULL, now) == now + 60.0);
}

/* The cache mode: one of three, the default the one that can't show a stale file. */
void test_asset_cache_mode(void)
{
    CHECK(wgr_asset_get_cache_mode() == WGR_ASSET_CACHE_REVALIDATE);
    CHECK(wgr_asset_set_cache_mode(WGR_ASSET_CACHE_TRUST));
    CHECK(wgr_asset_get_cache_mode() == WGR_ASSET_CACHE_TRUST);
    CHECK(wgr_asset_set_cache_mode(WGR_ASSET_CACHE_OFF));
    CHECK(wgr_asset_get_cache_mode() == WGR_ASSET_CACHE_OFF);
    CHECK(!wgr_asset_set_cache_mode((wgr_asset_cache_mode_t)3)); /* refused, and nothing changes */
    CHECK(!wgr_asset_set_cache_mode((wgr_asset_cache_mode_t)-1));
    CHECK(wgr_asset_get_cache_mode() == WGR_ASSET_CACHE_OFF);
    CHECK(wgr_asset_set_cache_mode(WGR_ASSET_CACHE_REVALIDATE));
    CHECK(wgr_asset_get_cache_mode() == WGR_ASSET_CACHE_REVALIDATE);
}

void test_asset_join_relative(void)
{
    check_join("models/box/box.gltf", "box.bin", "models/box/box.bin");
    check_join("models/box/box.gltf", "textures/wood.png", "models/box/textures/wood.png");
    check_join("models/box/box.gltf", "./textures/../wood.png", "models/box/wood.png");
    check_join("models/box/box.gltf", "../shared/wood.png", "models/shared/wood.png");
    check_join("models/box/box.gltf", "../../wood.png", "wood.png");
    check_join("models/box/box.gltf", "../../../wood.png", NULL);   /* above the asset root */
    check_join("models/box/box.gltf", "my%20wood%2Epng", "models/box/my wood.png"); /* %XX decoded */
    check_join("box.gltf", "box.bin", "box.bin");                   /* no directory */
    check_join("/wgr/models/box.gltf", "box.bin", "/wgr/models/box.bin"); /* leading / kept */
    check_join("models//box.gltf", "a.bin", "models/a.bin");        /* empty segments dropped */

    char small[8];
    CHECK(!wgri_asset_join_relative("models/box.gltf", "texture.png", small, sizeof(small))); /* doesn't fit */

    CHECK(wgri_asset_is_relative_uri("box.bin"));
    CHECK(wgri_asset_is_relative_uri("../textures/a.png"));
    CHECK(wgri_asset_is_relative_uri("dir/with:colon.png")); /* a colon after a slash isn't a scheme */
    CHECK(!wgri_asset_is_relative_uri("data:application/octet-stream;base64,AAAA"));
    CHECK(!wgri_asset_is_relative_uri("https://example.com/box.bin"));
    CHECK(!wgri_asset_is_relative_uri("/abs/box.bin"));
    CHECK(!wgri_asset_is_relative_uri(""));
    CHECK(!wgri_asset_is_relative_uri(NULL));
}

/* The fetch hook (docs/PLAN-asset-fetch.md): with a URL host and a fetcher, a desktop
 * miss becomes a download. No network here — the fetcher writes the file itself, which
 * is all libwgrender asks of it. */
static int fetch_calls;
static char fetched_url[512];

static void test_fetcher(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    FILE *f;
    fetch_calls++;
    snprintf(fetched_url, sizeof fetched_url, "%s", url);
    /* what a downloader does, minus the downloading: put bytes at dest_path */
    f = fopen(dest_path, "wb");
    if (f != NULL) {
        fputs("fetched", f);
        fclose(f);
    }
    wgr_asset_fetch_done(request, f != NULL && *(const bool *)user);
}

/* A fetcher that never answers, to hold a task in flight. */
static wgr_handle_t silent_request;
static void silent_fetcher(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    (void)url; (void)dest_path; (void)user;
    silent_request = request;
}

static int ready_count, failed_count;
static void on_ready(const char *path, void *user) { (void)path; (void)user; ready_count++; }
static void on_failed(const char *path, void *user) { (void)path; (void)user; failed_count++; }

void test_asset_fetch_hook(void)
{
    bool succeed = true;
    wgri_fs_init(NULL);
    wgri_asset_init();
    fetch_calls = ready_count = failed_count = 0;

    /* a local directory host behaves as it always has: no fetcher is consulted */
    CHECK(wgr_asset_set_cache_dir(WGR_TEST_DIR "/asset-cache"));
    CHECK(wgr_asset_set_fetcher(test_fetcher, &succeed));
    wgr_asset_set_host(WGR_TEST_DIR "/asset-cache");
    wgr_asset_add_task(wgr_asset_ensure_async("nothing/here.bin", NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed,
                       NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 0 && failed_count == 1);

    /* a URL host makes the same miss a download, and the callback gets a local path */
    remove(WGR_TEST_DIR "/asset-cache/textures/rock.png");
    wgr_asset_set_host("https://assets.example.com/game");
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed,
                       NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 1 && ready_count == 1);
    CHECK(strcmp(fetched_url, "https://assets.example.com/game/textures/rock.png") == 0);
    CHECK(wgri_fs_exists("textures/rock.png")); /* it landed in the cache dir */

    /* cached now: the next ensure resolves without asking the fetcher again */
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed,
                       NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 1 && ready_count == 2);

    /* a fetcher that reports failure fails the task rather than hanging it */
    succeed = false;
    remove(WGR_TEST_DIR "/asset-cache/textures/rock.png");
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", NULL, WGR_ASSET_FORCE_FETCH | WGR_ASSET_FILE_ONLY),
                       on_ready, on_failed, NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 2 && failed_count == 2);

    /* a cached file can be wrong rather than missing, so it has to be droppable:
       librl had rl_fs_remove/rl_fs_clear and parity dropped them with the rest of the
       filesystem, which left a bad copy unreachable (docs/TASKS.md) */
    succeed = true;
    fetch_calls = 0;
    CHECK(wgr_asset_evict("textures/rock.png")); /* the failed attempt left a file behind */
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed,
                       NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 1 && wgri_fs_exists("textures/rock.png"));

    CHECK(wgr_asset_evict("textures/rock.png"));
    CHECK(!wgri_fs_exists("textures/rock.png")); /* gone, so the next ensure fetches */
    CHECK(!wgr_asset_evict(NULL) && !wgr_asset_evict(""));

    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed,
                       NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 2); /* it went back to the fetcher rather than the cache */

    /* A per-call fetch_url is the download source, used verbatim -- the web path has
       always honoured it (start_fetch); desktop built host + path regardless, so a
       mirror or a signed link was silently ignored here. */
    fetch_calls = 0;
    CHECK(wgr_asset_evict("textures/rock.png"));
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", "https://mirror.example.net/signed/rock.png?sig=1",
                                              WGR_ASSET_FILE_ONLY),
                       on_ready, on_failed, NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 1);
    CHECK(strcmp(fetched_url, "https://mirror.example.net/signed/rock.png?sig=1") == 0);
    CHECK(wgri_fs_exists("textures/rock.png")); /* cached under the logical path, not the URL */

    /* and it doesn't need a URL host: a task told where to download from downloads */
    wgr_asset_set_host(WGR_TEST_DIR "/asset-cache");
    fetch_calls = 0;
    CHECK(wgr_asset_evict("textures/rock.png"));
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", "https://mirror.example.net/rock.png",
                                              WGR_ASSET_FILE_ONLY),
                       on_ready, on_failed, NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 1 && strcmp(fetched_url, "https://mirror.example.net/rock.png") == 0);

    /* a "://" redirect target is a download source too, on desktop as on the web */
    wgr_asset_set_host("https://assets.example.com/game");
    wgr_asset_add_redirect("textures/", "https://cdn.example.com/hd/textures/");
    fetch_calls = 0;
    CHECK(wgr_asset_evict("textures/rock.png"));
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed,
                       NULL);
    for (int i = 0; i < 8; i++) wgri_asset_tick();
    CHECK(fetch_calls == 1);
    CHECK(strcmp(fetched_url, "https://cdn.example.com/hd/textures/rock.png") == 0);
    wgr_asset_clear_redirects();

    /* clearing deletes what was downloaded into the cache directory, so the file is
       gone and there is nothing left to evict */
    wgr_asset_clear_cache();
    CHECK(!wgri_fs_exists("textures/rock.png"));
    CHECK(!wgr_asset_evict("textures/rock.png"));

    /* the pending report names a task and its stage; here: one download in flight */
    CHECK(wgri_asset_pending_count() == 0);
    wgr_asset_set_fetcher(silent_fetcher, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async("textures/rock.png", NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed,
                       NULL);
    wgri_asset_tick();
    CHECK(wgri_asset_pending_count() == 1);
    wgri_asset_pending_log(); /* logs "textures/rock.png (downloading, in flight)"; must not touch the task */
    CHECK(wgri_asset_pending_count() == 1);
    wgr_asset_fetch_done(silent_request, false); /* let it fail so deinit has nothing in flight */
    for (int i = 0; i < 4; i++) wgri_asset_tick();
    CHECK(wgri_asset_pending_count() == 0);

    wgr_asset_set_fetcher(NULL, NULL);
    wgr_asset_set_host("");
    wgri_asset_deinit();
    wgri_fs_deinit();
}
