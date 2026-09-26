/* The asset manifest on desktop (docs/PLAN-asset-cache.md): a URL host, a fetcher that
 * serves files from a directory standing in for the host, and manifests this test
 * writes the way tools/gen_manifest.py would. What each run downloads is the check:
 * the root manifest once a run, a directory's manifest and a file only when their
 * hashes changed, and nothing whose bytes don't match. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#include "internal/wgr_asset_internal.h"
#include "internal/wgr_fs_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_sha256_internal.h"
#include "test.h"
#include "tests.h"
#include "wgr_asset.h"

#define SERVER WGR_TEST_DIR "/manifest-server"
#define CACHE WGR_TEST_DIR "/manifest-cache"
#define HOST "https://assets.example.com/game"

static char fetch_log[1024]; /* the paths downloaded, each followed by ";" */
static int ready, failed;

static void on_ready(const char *path, void *user) { (void)path; (void)user; ready++; }
static void on_failed(const char *path, void *user) { (void)path; (void)user; failed++; }

static bool read_all(const char *path, char *out, size_t out_size, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    *size = fread(out, 1, out_size, f);
    fclose(f);
    return true;
}

static bool write_all(const char *path, const char *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    fwrite(data, 1, size, f);
    fclose(f);
    return true;
}

/* The host: HOST/<path> is SERVER/<path>; a missing file is a failed download. */
static void server_fetcher(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    char from[512], data[4096];
    size_t size = 0;
    const char *path = url + strlen(HOST "/");
    (void)user;
    strncat(fetch_log, path, sizeof(fetch_log) - strlen(fetch_log) - 1);
    strncat(fetch_log, ";", sizeof(fetch_log) - strlen(fetch_log) - 1);
    snprintf(from, sizeof(from), SERVER "/%s", path);
    wgr_asset_fetch_done(request, read_all(from, data, sizeof(data), &size) && write_all(dest_path, data, size));
}

static void hash_of(const char *text, char out[WGRI_SHA256_TEXT])
{
    wgri_sha256_text((const unsigned char *)text, strlen(text), out);
}

/* Deploy a host: textures/a.png and b.png, listed in textures/manifest.json, which
 * the root lists by its hash, and top.txt beside the root. `served_b` (or NULL) is
 * what the host actually serves for b.png, whatever the manifest says. */
static void deploy(const char *a, const char *b, const char *served_b)
{
    char ha[WGRI_SHA256_TEXT], hb[WGRI_SHA256_TEXT], htop[WGRI_SHA256_TEXT], hdir[WGRI_SHA256_TEXT];
    char dir_manifest[512], root[512];
    mkdir(SERVER, 0755);
    mkdir(SERVER "/textures", 0755);
    write_all(SERVER "/textures/a.png", a, strlen(a));
    write_all(SERVER "/textures/b.png", served_b ? served_b : b, strlen(served_b ? served_b : b));
    write_all(SERVER "/textures/c.png", "unlisted", 8);
    write_all(SERVER "/top.txt", "top", 3);
    hash_of(a, ha);
    hash_of(b, hb);
    hash_of("top", htop);
    snprintf(dir_manifest, sizeof(dir_manifest),
             "{\"wgr_manifest\": 1, \"files\": {\"a.png\": \"%s\", \"b.png\": \"%s\"}, \"dirs\": {}}\n", ha, hb);
    write_all(SERVER "/textures/manifest.json", dir_manifest, strlen(dir_manifest));
    hash_of(dir_manifest, hdir);
    snprintf(root, sizeof(root), "{\"wgr_manifest\": 1, \"files\": {\"top.txt\": \"%s\"}, \"dirs\": {\"textures\": \"%s\"}}\n",
             htop, hdir);
    write_all(SERVER "/manifest.json", root, strlen(root));
}

/* One ensure, run to its end; what it downloaded is returned (and the log reset). */
static const char *ensure(const char *path)
{
    static char log[1024];
    fetch_log[0] = '\0';
    wgr_asset_add_task(wgr_asset_ensure_async(path, NULL, WGR_ASSET_FILE_ONLY), on_ready, on_failed, NULL);
    for (int i = 0; i < 16; i++) wgri_asset_tick();
    snprintf(log, sizeof(log), "%s", fetch_log);
    return log;
}

/* A new run of the program: what was read of the manifests this run is forgotten,
 * the cache directory stays. */
static void new_run(void)
{
    wgri_asset_deinit();
    wgri_asset_init();
}

static bool log_is(const char *got, const char *expected)
{
    if (strcmp(got, expected) == 0) return true;
    fprintf(stderr, "    downloaded \"%s\", expected \"%s\"\n", got, expected);
    return false;
}

void test_asset_manifest(void)
{
    static const char *const stale[] = {
        CACHE "/manifest.json", CACHE "/top.txt", CACHE "/textures/manifest.json", CACHE "/textures/a.png",
        CACHE "/textures/b.png", CACHE "/textures/c.png", CACHE "/.meta/manifest.json", CACHE "/.meta/top.txt",
        CACHE "/.meta/textures/manifest.json", CACHE "/.meta/textures/a.png", CACHE "/.meta/textures/b.png",
    };
    for (size_t i = 0; i < sizeof(stale) / sizeof(stale[0]); i++) remove(stale[i]);

    wgri_fs_init(NULL);
    wgri_asset_init();
    CHECK(wgr_asset_set_cache_dir(CACHE));
    wgr_asset_set_host(HOST);
    wgr_asset_set_fetcher(server_fetcher, NULL);
    ready = failed = 0;

    /* what a manifest path may be */
    CHECK(!wgr_asset_set_manifest("/manifest.json"));
    CHECK(!wgr_asset_set_manifest("https://assets.example.com/manifest.json"));
    CHECK(wgr_asset_set_manifest(NULL));
    CHECK(wgr_asset_set_manifest("manifest.json"));

    /* the first run: the root, the directory's manifest, the file */
    deploy("A1", "B1", NULL);
    CHECK(log_is(ensure("textures/a.png"), "manifest.json;textures/manifest.json;textures/a.png;"));
    CHECK(ready == 1);
    /* the manifests are read once a run; a sibling costs only itself */
    CHECK(log_is(ensure("textures/b.png"), "textures/b.png;"));
    /* and a file already here with its listed hash costs nothing */
    CHECK(log_is(ensure("textures/a.png"), ""));
    CHECK(log_is(ensure("top.txt"), "top.txt;"));
    CHECK(ready == 4 && failed == 0);

    /* the next run asks for the root alone: nothing under it changed */
    new_run();
    CHECK(log_is(ensure("textures/a.png"), "manifest.json;"));
    CHECK(log_is(ensure("textures/b.png"), ""));

    /* a deploy that changes b: its directory's manifest and b, nothing else */
    deploy("A1", "B2", NULL);
    new_run();
    CHECK(log_is(ensure("textures/a.png"), "manifest.json;textures/manifest.json;"));
    CHECK(log_is(ensure("textures/b.png"), "textures/b.png;"));
    CHECK(log_is(ensure("top.txt"), ""));
    CHECK(ready == 9 && failed == 0);

    /* a host that serves bytes the manifest doesn't list: not kept, and the load fails */
    deploy("A1", "B3", "B2 still");
    new_run();
    ensure("textures/b.png");
    CHECK(failed == 1);
    CHECK(!wgri_fs_exists("textures/b.png"));
    /* until the host catches up */
    deploy("A1", "B3", NULL);
    CHECK(log_is(ensure("textures/b.png"), "textures/b.png;"));
    CHECK(ready == 10 && failed == 1);

    /* a listed file the host no longer has: the old copy isn't used in its place */
    deploy("A4", "B3", NULL);
    remove(SERVER "/textures/a.png");
    new_run();
    CHECK(log_is(ensure("textures/a.png"), "manifest.json;textures/manifest.json;textures/a.png;"));
    CHECK(failed == 2);
    CHECK(log_is(ensure("textures/a.png"), "textures/a.png;")); /* asked again, not taken from the cache */
    CHECK(failed == 3);

    /* the host unreachable for the root: the one from before is used */
    deploy("A1", "B3", NULL);
    new_run();
    ensure("textures/a.png"); /* this run's root lists A1 again */
    remove(SERVER "/manifest.json");
    new_run();
    CHECK(log_is(ensure("textures/b.png"), "manifest.json;"));
    CHECK(log_is(ensure("textures/a.png"), ""));
    CHECK(ready == 13 && failed == 3);

    /* a file no manifest lists is cached as before: fetched once, then trusted */
    CHECK(log_is(ensure("textures/c.png"), "textures/c.png;"));
    new_run();
    CHECK(log_is(ensure("textures/c.png"), "manifest.json;"));
    CHECK(ready == 15);

    /* a root that isn't a manifest lists nothing: everything is cached as the mode says */
    write_all(SERVER "/manifest.json", "{\"wgr_manifest\": 2}", 19);
    write_all(SERVER "/textures/b.png", "B5", 2);
    new_run();
    CHECK(log_is(ensure("textures/b.png"), "manifest.json;")); /* trusted: here already */
    CHECK(ready == 16);

    wgr_asset_set_manifest(NULL);
    wgr_asset_set_fetcher(NULL, NULL);
    wgr_asset_set_host("");
    wgri_asset_deinit();
    wgri_fs_deinit();
}
