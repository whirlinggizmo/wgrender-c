#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h> /* _getcwd */
#else
#include <unistd.h> /* getcwd */
#endif

#include "internal/wgr_fs_internal.h"
#include "internal/wgr_thread_internal.h"
#include "internal/wgr_internal_internal.h"
#include "wgr_asset.h"

#include "internal/wgr_asset_internal.h"
#include "test.h"
#include "test_assets.h"
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

static void check_normalize(const char *path, const char *expected)
{
    char out[64];
    const bool ok = wgri_asset_normalize_path(path, out, sizeof(out));
    CHECK(ok == (expected != NULL));
    if (ok && expected != NULL && strcmp(out, expected) != 0) {
        fprintf(stderr, "    normalize(%s): got %s, expected %s\n", path, out, expected);
        wgr_test_failures++;
    }
}

/* What a program may name (ensure, evict, redirects): paths under the asset root,
 * read as wgutils' fileio reads them. */
void test_asset_paths(void)
{
    check_normalize("textures/rock.png", "textures/rock.png");
    check_normalize("./textures//rock.png", "textures/rock.png");
    check_normalize("textures\\rock.png", "textures/rock.png");         /* Windows separators */
    check_normalize("textures/../models/box.glb", "models/box.glb");    /* ".." within the root */
    check_normalize("a/b/../../c", "c");
    check_normalize("textures/", "textures");

    check_normalize("/etc/passwd", NULL);           /* absolute */
    check_normalize("\\server\\share", NULL);
    check_normalize("C:/Windows/win.ini", NULL);     /* a drive */
    check_normalize("C:foo", NULL);
    check_normalize("textures/a:b.png", NULL);       /* any ":" */
    check_normalize("../secret", NULL);              /* above the root */
    check_normalize("textures/../../secret", NULL);
    check_normalize("", NULL);                       /* names nothing */
    check_normalize(".", NULL);
    check_normalize("a/..", NULL);
    check_normalize("0123456789/0123456789/0123456789/0123456789/0123456789/0123456789", NULL); /* too long */
    check_normalize(NULL, NULL);

    wgri_fs_init(NULL);
    wgri_asset_init();
    CHECK(wgr_asset_ensure("/etc/passwd", NULL, WGR_ASSET_NONE) == 0);
    CHECK(wgr_asset_ensure("../outside.png", NULL, WGR_ASSET_NONE) == 0);
    CHECK(wgr_asset_ensure("C:/x.png", NULL, WGR_ASSET_NONE) == 0);
    CHECK(!wgr_asset_evict("../../etc/passwd"));
    CHECK(!wgr_asset_add_redirect("textures/", "../mods/"));
    CHECK(!wgr_asset_add_redirect("/textures/", "mods/"));
    CHECK(wgr_asset_add_redirect("./textures/", "mods/hd/../sd/"));  /* normalized: textures/ -> mods/sd/ */
    CHECK(wgr_asset_add_redirect("models/", "https://cdn.example.com/../models/")); /* a URL is left as it is */
    wgr_asset_clear_redirects();
    wgri_asset_deinit();
    wgri_fs_deinit();
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
    check_join("models/box/box.gltf", "..\\shared\\wood.png", "models/shared/wood.png"); /* "\\" separates */
    check_join("models\\box\\box.gltf", "wood.png", "models/box/wood.png");  /* in the base too */
    check_join("models/box/box.gltf", "..\\..\\..\\wood.png", NULL);   /* so it can't climb out */
    check_join("models/box/box.gltf", "..%5C..%5C..%5Cwood.png", NULL); /* decoded or not */
    check_join("models/box/box.gltf", "../..\\../wood.png", NULL);     /* or mixed */
    check_join("models/box/box.gltf", "a/../C:/wood.png", NULL);     /* a drive in the uri */
    check_join("models/box/box.gltf", "C%3A/wood.png", NULL);        /* encoded */
    check_join("C:/game/models/box.gltf", "../wood.png", "C:/game/wood.png"); /* a base's drive is its own */

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

/* Fetching (wgr_asset_set_fetching, include/wgr_asset.h): with a URL host and the program
 * downloading, a desktop miss becomes a request (test_assets_tick takes them). No network here — the fetcher writes the file itself, which
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

/* Ensure a file and tick until it finishes: whether it was DONE, FAILED (both 0 or 1), and its path. */
static int ready_count, failed_count;
static char completed_path[512];
static void ensure_and_tick(const char *key, const char *source, unsigned flags)
{
    const wgr_asset_task_status_t status = test_assets_ensure(key, source, flags, 8);
    ready_count = status == WGR_ASSET_TASK_DONE;
    failed_count = status == WGR_ASSET_TASK_FAILED;
    snprintf(completed_path, sizeof(completed_path), "%s", test_assets_path);
}

void test_asset_fetch_hook(void)
{
    bool succeed = true;
    wgri_fs_init(NULL);
    wgri_asset_init();
    fetch_calls = ready_count = failed_count = 0;

    /* a local directory host behaves as it always has: no fetcher is consulted */
    CHECK(wgr_asset_set_cache_dir(WGR_TEST_DIR "/asset-cache"));
    CHECK(test_assets_set_fetcher(test_fetcher, &succeed));
    wgr_asset_set_host(WGR_TEST_DIR "/asset-cache");
    ensure_and_tick("nothing/here.bin", NULL, WGR_ASSET_NONE);
    CHECK(fetch_calls == 0 && failed_count == 1);

    /* a URL host makes the same miss a download, and the task has a local path */
    remove(WGR_TEST_DIR "/asset-cache/textures/rock.png");
    wgr_asset_set_host("https://assets.example.com/game");
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_NONE);
    CHECK(fetch_calls == 1 && ready_count == 1);
    CHECK(strcmp(fetched_url, "https://assets.example.com/game/textures/rock.png") == 0);
    CHECK(wgri_fs_exists("textures/rock.png")); /* it landed in the cache dir */

    /* cached now: the next ensure resolves without asking the fetcher again */
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_NONE);
    CHECK(fetch_calls == 1 && ready_count == 1);

    /* a fetcher that reports failure fails the task rather than hanging it */
    succeed = false;
    remove(WGR_TEST_DIR "/asset-cache/textures/rock.png");
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_FORCE_FETCH);
    CHECK(fetch_calls == 2 && failed_count == 1);

    /* a cached file can be wrong rather than missing, so it has to be droppable:
       librl had rl_fs_remove/rl_fs_clear and parity dropped them with the rest of the
       filesystem, which left a bad copy unreachable (docs/TASKS.md) */
    succeed = true;
    fetch_calls = 0;
    CHECK(!wgr_asset_evict("textures/rock.png")); /* a failed download leaves nothing behind */
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_NONE);
    CHECK(fetch_calls == 1 && wgri_fs_exists("textures/rock.png"));

    CHECK(wgr_asset_evict("textures/rock.png"));
    CHECK(!wgri_fs_exists("textures/rock.png")); /* gone, so the next ensure fetches */
    CHECK(!wgr_asset_evict(NULL) && !wgr_asset_evict(""));

    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_NONE);
    CHECK(fetch_calls == 2); /* it went back to the fetcher rather than the cache */

    /* A per-call fetch_url is the download source, used verbatim -- the web path has
       always honoured it (start_fetch); desktop built host + path regardless, so a
       mirror or a signed link was silently ignored here. */
    fetch_calls = 0;
    CHECK(wgr_asset_evict("textures/rock.png"));
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/signed/rock.png?sig=1", WGR_ASSET_NONE);
    CHECK(fetch_calls == 1);
    CHECK(strcmp(fetched_url, "https://mirror.example.net/signed/rock.png?sig=1") == 0);
    CHECK(wgri_fs_exists("textures/rock.png")); /* cached under the logical path, not the URL */

    /* and it doesn't need a URL host: a task told where to download from downloads */
    wgr_asset_set_host(WGR_TEST_DIR "/asset-cache");
    fetch_calls = 0;
    CHECK(wgr_asset_evict("textures/rock.png"));
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", WGR_ASSET_NONE);
    CHECK(fetch_calls == 1 && strcmp(fetched_url, "https://mirror.example.net/rock.png") == 0);

    /* a "://" redirect target is a download source too, on desktop as on the web */
    wgr_asset_set_host("https://assets.example.com/game");
    wgr_asset_add_redirect("textures/", "https://cdn.example.com/hd/textures/");
    fetch_calls = 0;
    CHECK(wgr_asset_evict("textures/rock.png"));
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_NONE);
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
    test_assets_set_fetcher(silent_fetcher, NULL);
    {
        const wgr_handle_t task = wgr_asset_ensure("textures/rock.png", NULL, WGR_ASSET_NONE);
        test_assets_tick();
        CHECK(wgri_asset_pending_count() == 1);
        wgri_asset_pending_log(); /* logs "textures/rock.png (downloading, in flight)"; must not touch the task */
        CHECK(wgri_asset_pending_count() == 1);
        wgr_asset_fetch_done(silent_request, false); /* let it fail so deinit has nothing in flight */
        for (int i = 0; i < 4; i++) test_assets_tick();
        CHECK(wgri_asset_pending_count() == 0); /* a finished task isn't pending, though it is kept */
        CHECK(wgr_asset_task_get_status(task) == WGR_ASSET_TASK_FAILED);
        CHECK(wgr_asset_task_destroy(task));
    }

    test_assets_set_fetcher(NULL, NULL);
    wgr_asset_set_host("");
    wgri_asset_deinit();
    wgri_fs_deinit();
}

/* A format whose whole file is the one URI it depends on. */
static void list_one_uri(const unsigned char *data, int size, wgri_asset_add_dependency_fn add, void *context)
{
    char uri[256];
    snprintf(uri, sizeof(uri), "%.*s", size, (const char *)data);
    add(uri, NULL, true, context);
}

#define JAIL_HOST WGR_TEST_DIR "/jail/assets"

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs(text, f);
    fclose(f);
}

/* Ensure a file naming `uri` as its dependency; true when it and the dependency load. */
static bool dependency_loads(const char *key, const char *uri)
{
    char full[512];
    wgri_fs_make_parents(key);
    wgri_fs_resolve(key, full, sizeof(full));
    write_text(full, uri);
    ensure_and_tick(key, NULL, WGR_ASSET_NONE);
    CHECK(ready_count + failed_count == 1);
    fprintf(stderr, "    %s -> %s: %s\n", key, uri, ready_count == 1 ? "loaded" : "refused");
    return ready_count == 1;
}

/* A dependency stays under the host, as a key does (wgri_asset_normalize_path): models
 * can share a directory elsewhere in the tree, but a file can't reach out of it. "\\"
 * is a separator on Windows, so it has to be one here too, or "..\\..\\" climbs out
 * there and nowhere else. */
void test_asset_dependency_jail(void)
{
    wgri_fs_init(NULL);
    wgri_asset_init();
    wgri_asset_register_dependencies(".dep", list_one_uri);
    wgr_asset_set_host(JAIL_HOST);

    wgri_fs_make_parents("shared.bin");
    write_text(JAIL_HOST "/shared.bin", "shared");
    write_text(WGR_TEST_DIR "/jail/secret.bin", "outside the host");

    CHECK(dependency_loads("models/shares.dep", "../shared.bin")); /* elsewhere in the tree */
    CHECK(!dependency_loads("models/climbs.dep", "../../secret.bin"));
    CHECK(!dependency_loads("models/backslash.dep", "..\\..\\secret.bin"));
    CHECK(!dependency_loads("models/encoded.dep", "..%5C..%5Csecret.bin"));
    CHECK(!dependency_loads("models/mixed.dep", "../..\\secret.bin"));

    wgr_asset_set_host("");
    wgri_asset_deinit();
    wgri_fs_deinit();
}

static void check_source(const char *host, wgri_asset_host_kind_t kind, const char *ref, wgri_asset_source_t want,
                         const char *expected)
{
    char out[512];
    const wgri_asset_source_t got = wgri_asset_resolve_source(host, kind, ref, out, sizeof(out));
    CHECK(got == want);
    if (got != want || (expected != NULL && strcmp(out, expected) != 0)) {
        fprintf(stderr, "    resolve(%s, %s): got %d \"%s\", expected %d \"%s\"\n", host, ref, (int)got, out, (int)want,
                expected != NULL ? expected : "");
        if (got == want) wgr_test_failures++;
    }
}

/* A fetch_url is read against the host as a browser reads a URL against a directory;
 * the URL cases' expectations are what the WHATWG URL parser gives (node's URL). */
void test_asset_resolve_source(void)
{
    const char *cdn = "https://cdn.example.com/game";
    const wgri_asset_source_t url = WGRI_SOURCE_URL, local = WGRI_SOURCE_LOCAL, refused = WGRI_SOURCE_REFUSED;

    /* a URL host, as the browser resolves against it */
    check_source(cdn, WGRI_HOST_URL, "music/a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "../shared/a.mp3", url, "https://cdn.example.com/shared/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "../../../a.mp3", url, "https://cdn.example.com/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "/other/a.mp3", url, "https://cdn.example.com/other/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "//mirror.example.net/a.mp3", url, "https://mirror.example.net/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "music/a.mp3?sig=1#t", url, "https://cdn.example.com/game/music/a.mp3?sig=1#t");
    check_source(cdn, WGRI_HOST_URL, "?v=2", url, "https://cdn.example.com/game/?v=2");
    check_source(cdn, WGRI_HOST_URL, "./music/./x/../a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "music\\a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "..\\shared\\a.mp3", url, "https://cdn.example.com/shared/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "music/", url, "https://cdn.example.com/game/music/");
    check_source(cdn, WGRI_HOST_URL, "music/..", url, "https://cdn.example.com/game/");
    check_source("https://cdn.example.com", WGRI_HOST_URL, "music/a.mp3", url, "https://cdn.example.com/music/a.mp3");
    check_source("http://localhost:8000/assets", WGRI_HOST_URL, "music/a.mp3", url,
                 "http://localhost:8000/assets/music/a.mp3"); /* a port is part of the origin */
    check_source("http://localhost:8000/assets", WGRI_HOST_URL, "../x/a.mp3", url, "http://localhost:8000/x/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "https://other.example.org/a.mp3", url, "https://other.example.org/a.mp3");
    check_source(cdn, WGRI_HOST_URL, "HTTPS://other.example.org/a.mp3", url, "HTTPS://other.example.org/a.mp3");

    /* on desktop an absolute source is http or https: nothing handed over names a local file */
    check_source(cdn, WGRI_HOST_URL, "file:///etc/passwd", refused, NULL);
    check_source(cdn, WGRI_HOST_URL, "data:application/octet-stream,AAAA", refused, NULL);
    check_source(cdn, WGRI_HOST_URL, "C:/Windows/win.ini", refused, NULL); /* "c:" is a scheme */

    /* the web's relative host: relative out, and the page finishes it as it would have
       (page /game/index.html: assets/music/a.mp3, /game/x.mp3, /x.mp3, ...) */
    check_source("assets", WGRI_HOST_BROWSER, "music/a.mp3", url, "assets/music/a.mp3");
    check_source("assets", WGRI_HOST_BROWSER, "../x.mp3", url, "x.mp3");
    check_source("assets", WGRI_HOST_BROWSER, "../../x.mp3", url, "../x.mp3");
    check_source("assets", WGRI_HOST_BROWSER, "/root.mp3", url, "/root.mp3");
    check_source("assets", WGRI_HOST_BROWSER, "//cdn.example.com/a.mp3", url, "//cdn.example.com/a.mp3");
    check_source("assets", WGRI_HOST_BROWSER, "?q=1", url, "assets/?q=1");
    check_source("", WGRI_HOST_BROWSER, "music/a.mp3", url, "music/a.mp3");
    check_source("/static/assets", WGRI_HOST_BROWSER, "../../../x.mp3", url, "/x.mp3");
    check_source(cdn, WGRI_HOST_BROWSER, "music/a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source("assets", WGRI_HOST_BROWSER, "data:application/octet-stream,AAAA", url,
                 "data:application/octet-stream,AAAA"); /* the browser's business */

    /* a local host: a path under it, held to a key's rules, read where it is */
    check_source("assets", WGRI_HOST_LOCAL, "music/a.mp3", local, "music/a.mp3");
    check_source("assets", WGRI_HOST_LOCAL, "music/a.mp3?sig=1#t", local, "music/a.mp3");
    check_source("assets", WGRI_HOST_LOCAL, "music/my%20song.mp3", local, "music/my song.mp3");
    check_source("assets", WGRI_HOST_LOCAL, "music/../shared/a.mp3", local, "shared/a.mp3");
    check_source("assets", WGRI_HOST_LOCAL, "music\\a.mp3", local, "music/a.mp3");
    check_source("assets", WGRI_HOST_LOCAL, "../a.mp3", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "..\\a.mp3", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "%2e%2e/a.mp3", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "..%5Ca.mp3", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "/etc/passwd", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "//server/share/a.mp3", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "file:///etc/passwd", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "a%00.mp3", refused, NULL);
    check_source("assets", WGRI_HOST_LOCAL, "https://cdn.example.com/a.mp3", url,
                 "https://cdn.example.com/a.mp3"); /* a download, for the fetcher */

    check_source(cdn, WGRI_HOST_URL, "", refused, NULL);
    check_source(cdn, WGRI_HOST_URL, NULL, refused, NULL);

    char dir[256];
    CHECK(wgri_asset_file_url_path("file:///opt/game/assets", dir, sizeof(dir)) && strcmp(dir, "/opt/game/assets") == 0);
    CHECK(wgri_asset_file_url_path("file://localhost/opt/game", dir, sizeof(dir)) && strcmp(dir, "/opt/game") == 0);
    CHECK(wgri_asset_file_url_path("FILE:///opt/my%20game", dir, sizeof(dir)) && strcmp(dir, "/opt/my game") == 0);
#if defined(_WIN32)
    CHECK(wgri_asset_file_url_path("file:///C:/games/assets", dir, sizeof(dir)) && strcmp(dir, "C:/games/assets") == 0);
#endif
    CHECK(!wgri_asset_file_url_path("file://server/share", dir, sizeof(dir))); /* another machine's */
    CHECK(!wgri_asset_file_url_path("https://cdn.example.com/game", dir, sizeof(dir)));
    CHECK(!wgri_asset_file_url_path("file:///opt/a%00b", dir, sizeof(dir)));
}

/* JAIL_HOST as a file: URL: absolute, "/" throughout ("file:///C:/..." on Windows). */
static void jail_file_url(char *out, size_t out_size)
{
#if defined(_WIN32)
    char *cwd = _getcwd(NULL, 0);
#else
    char *cwd = getcwd(NULL, 0);
#endif
    snprintf(out, out_size, "file://%s%s/%s", cwd != NULL && cwd[0] != '/' ? "/" : "", cwd != NULL ? cwd : "",
             JAIL_HOST);
    for (char *c = out; *c != '\0'; c++) {
        if (*c == '\\') *c = '/';
    }
    free(cwd);
}

/* A relative fetch_url under a local host is read in place, under the key's name; one
 * that would leave the host, or names a local file by URL, is refused like a bad key.
 * The same, spelled as a file: URL host. */
void test_asset_local_source(void)
{
    char where[512];
    wgri_fs_init(NULL);
    wgri_asset_init();

    for (int spelling = 0; spelling < 2; spelling++) {
        char host[512];
        if (spelling == 0) {
            snprintf(host, sizeof(host), "%s", JAIL_HOST);
        } else {
            jail_file_url(host, sizeof(host));
        }
        wgr_asset_set_host(host);
        wgri_fs_make_parents("music/real.mp3");
        wgri_fs_resolve("music/real.mp3", where, sizeof(where));
        write_text(where, "the real bytes");

        ensure_and_tick("music/invalid.mp3", "music/real.mp3?v=2", WGR_ASSET_NONE);
        CHECK(ready_count == 1 && strcmp(completed_path, where) == 0); /* the path is the file found */
        CHECK(!wgri_fs_exists("music/invalid.mp3")); /* read in place: nothing copied under the key */
        {
            char key[512], found[512];
            wgri_fs_resolve("music/invalid.mp3", key, sizeof(key));
            CHECK(wgri_asset_found_path(key, found, sizeof(found)) && strcmp(found, where) == 0);
        }

        CHECK(wgr_asset_ensure("music/x.mp3", "../secret.bin", WGR_ASSET_NONE) == 0);
        CHECK(wgr_asset_ensure("music/x.mp3", "file:///etc/passwd", WGR_ASSET_NONE) == 0);
        CHECK(wgr_asset_ensure("music/x.mp3", "/etc/passwd", WGR_ASSET_NONE) == 0);
    }

    wgr_asset_set_host("");
    wgri_asset_deinit();
    wgri_fs_deinit();
}

#define RO_HOST WGR_TEST_DIR "/ro/assets"
#define RO_CACHE WGR_TEST_DIR "/ro/cache"

static bool read_back(const char *path, const char *want)
{
    char got[64] = "";
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    if (fgets(got, sizeof(got), f) == NULL) got[0] = '\0';
    fclose(f);
    return strcmp(got, want) == 0;
}

/* A local host is only ever read, as a browser only reads its host: what a task
 * downloads from a URL of its own lands in the cache, and counts there only for that
 * URL, so a shipped file is never overwritten or evicted, and an old download never
 * hides it. */
void test_asset_readonly_host(void)
{
    bool succeed = true;
    const char *shipped = RO_HOST "/textures/rock.png";
    const char *cached = RO_CACHE "/textures/rock.png";

    wgri_fs_init(NULL);
    wgri_asset_init();
    CHECK(wgr_asset_set_cache_dir(RO_CACHE));
    wgr_asset_set_host(RO_HOST);
    wgri_fs_make_parents("textures/rock.png");
    write_text(shipped, "shipped");
    remove(cached);
    CHECK(test_assets_set_fetcher(test_fetcher, &succeed));
    fetch_calls = 0;

    /* downloaded into the cache, not over the shipped file */
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    CHECK(fetch_calls == 1 && ready_count == 1);
    CHECK(read_back(cached, "fetched"));
    CHECK(read_back(shipped, "shipped"));
    CHECK(strcmp(completed_path, cached) == 0);

    /* the same source again: the cached copy, no request */
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    CHECK(fetch_calls == 1 && ready_count == 1);

    /* another source for the same key: that copy isn't this one */
    ensure_and_tick("textures/rock.png", "https://other.example.net/rock.png", 0);
    CHECK(fetch_calls == 2 && ready_count == 1);

    /* a plain ensure reads the host, whatever the cache holds */
    ensure_and_tick("textures/rock.png", NULL, 0);
    CHECK(fetch_calls == 2 && ready_count == 1 && strcmp(completed_path, shipped) == 0);

    /* forced: downloaded again, still into the cache */
    ensure_and_tick("textures/rock.png", "https://other.example.net/rock.png", WGR_ASSET_FORCE_FETCH);
    CHECK(fetch_calls == 3 && ready_count == 1 && read_back(shipped, "shipped"));

    /* evicting and clearing touch the cache, never the host */
    CHECK(wgr_asset_evict("textures/rock.png"));
    CHECK(!wgri_fs_exists(WGRI_FS_CACHE "textures/rock.png") && read_back(shipped, "shipped"));
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    CHECK(fetch_calls == 4 && read_back(cached, "fetched"));
    wgr_asset_clear_cache();
    CHECK(!read_back(cached, "fetched") && read_back(shipped, "shipped"));
    CHECK(!wgr_asset_evict("textures/rock.png")); /* nothing of the cache's left */
    CHECK(read_back(shipped, "shipped"));

    /* a URL redirect downloads only what the host hasn't, and into the cache */
    CHECK(wgr_asset_add_redirect("textures/", "https://cdn.example.com/textures/"));
    fetch_calls = 0;
    ensure_and_tick("textures/rock.png", NULL, 0);
    CHECK(fetch_calls == 0 && ready_count == 1 && strcmp(completed_path, shipped) == 0);
    remove(RO_CACHE "/textures/missing.png");
    ensure_and_tick("textures/missing.png", NULL, 0);
    CHECK(fetch_calls == 1 && ready_count == 1);
    CHECK(strcmp(fetched_url, "https://cdn.example.com/textures/missing.png") == 0);
    CHECK(read_back(RO_CACHE "/textures/missing.png", "fetched"));
    CHECK(!read_back(RO_HOST "/textures/missing.png", "fetched"));
    wgr_asset_clear_redirects();

    /* no fetcher: a URL of its own can't be had, and the host's file isn't it */
    test_assets_set_fetcher(NULL, NULL);
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    CHECK(ready_count == 0 && failed_count == 1);

    wgr_asset_set_host("");
    wgri_asset_deinit();
    wgri_fs_deinit();
}

#define BROKEN_CACHE WGR_TEST_DIR "/broken-cache"

/* What the broken-download fetcher does with its destination. */
enum { WRITE_AND_SUCCEED, WRITE_NEWER_AND_SUCCEED, WRITE_HALF_AND_FAIL, SUCCEED_WITHOUT_WRITING };
static int broken_mode;
static char broken_dest[1100];

static void broken_fetcher(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    (void)url;
    (void)user;
    fetch_calls++;
    snprintf(broken_dest, sizeof(broken_dest), "%s", dest_path);
    if (broken_mode != SUCCEED_WITHOUT_WRITING) {
        write_text(dest_path, broken_mode == WRITE_AND_SUCCEED ? "fetched"
                              : broken_mode == WRITE_NEWER_AND_SUCCEED ? "newer" : "hal");
    }
    wgr_asset_fetch_done(request, broken_mode != WRITE_HALF_AND_FAIL);
}

/* A download is written apart (.part/) and takes the file's place only when the fetcher
 * says it worked, so a failed, empty or interrupted one never leaves half a file where
 * one is read, and never costs the copy that was there. */
void test_asset_broken_download(void)
{
    const char *final = BROKEN_CACHE "/textures/rock.png";
    const char *partial = BROKEN_CACHE "/.part/textures/rock.png";

    wgri_fs_init(NULL);
    wgri_asset_init();
    CHECK(wgr_asset_set_cache_dir(BROKEN_CACHE));
    wgr_asset_set_host("https://assets.example.com/game");
    CHECK(test_assets_set_fetcher(broken_fetcher, NULL));
    remove(final);
    fetch_calls = 0;

    /* the fetcher writes apart, and the file is moved into place */
    broken_mode = WRITE_AND_SUCCEED;
    ensure_and_tick("textures/rock.png", NULL, 0);
    CHECK(fetch_calls == 1 && ready_count == 1);
    CHECK(strstr(broken_dest, "/.part/") != NULL);
    CHECK(read_back(final, "fetched"));
    CHECK(!read_back(partial, "fetched")); /* moved, not copied */

    /* a download that fails partway: the copy that was there stays, the half goes */
    broken_mode = WRITE_HALF_AND_FAIL;
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_FORCE_FETCH);
    CHECK(fetch_calls == 2 && failed_count == 1);
    CHECK(read_back(final, "fetched"));
    CHECK(!read_back(partial, "hal"));

    /* a fetcher that says it worked but wrote nothing: failed, and nothing replaced */
    broken_mode = SUCCEED_WITHOUT_WRITING;
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_FORCE_FETCH);
    CHECK(fetch_calls == 3 && failed_count == 1);
    CHECK(read_back(final, "fetched"));

    /* what an interrupted run left is never taken for a download */
    wgri_fs_make_parents(WGRI_FS_CACHE ".part/textures/rock.png");
    write_text(partial, "junk");
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_FORCE_FETCH);
    CHECK(fetch_calls == 4 && failed_count == 1);
    CHECK(read_back(final, "fetched") && !read_back(partial, "junk"));

    /* and a download that works replaces the copy that was there (on Windows too, where
       a rename won't) */
    broken_mode = WRITE_NEWER_AND_SUCCEED;
    ensure_and_tick("textures/rock.png", NULL, WGR_ASSET_FORCE_FETCH);
    CHECK(fetch_calls == 5 && ready_count == 1);
    CHECK(read_back(final, "newer"));

    test_assets_set_fetcher(NULL, NULL);
    wgr_asset_set_host("");
    wgri_asset_deinit();
    wgri_fs_deinit();
}

#define ASYNC_CACHE WGR_TEST_DIR "/async-cache"

/* A download answered from a thread of the fetcher's own, as a real one would be. */
typedef struct {
    wgr_handle_t request;
    char dest[1100];
} async_download_t;
static async_download_t async_download;
static wgri_thread_t async_thread;

static void async_worker(void *arg)
{
    const async_download_t *d = (const async_download_t *)arg;
    write_text(d->dest, "fetched");
    wgr_asset_fetch_done(d->request, true); /* from this thread, not the main one */
}

static void threaded_fetcher(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    (void)url;
    (void)user;
    fetch_calls++;
    async_download.request = request;
    snprintf(async_download.dest, sizeof(async_download.dest), "%s", dest_path);
    CHECK(wgri_thread_create(&async_thread, async_worker, &async_download));
}

/* Requests held, to answer later: how many are out at once. */
static wgr_handle_t held[16];
static int held_count;
static void holding_fetcher(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    (void)url;
    (void)user;
    write_text(dest_path, "fetched");
    if (held_count < 16) held[held_count++] = request;
}

static int count_done(const wgr_handle_t *tasks, int count)
{
    int done = 0;
    for (int i = 0; i < count; i++) done += wgr_asset_task_get_status(tasks[i]) == WGR_ASSET_TASK_DONE;
    return done;
}

/* A fetcher may answer from any thread, as a browser's fetch reports back: the answer
 * is taken at the next tick, on the main thread. And a fetcher has at most 6 downloads
 * out at once, a browser's limit per server; the rest wait their turn. */
void test_asset_async_fetch(void)
{
    char key[64];
    wgr_handle_t tasks[10];
    wgri_fs_init(NULL);
    wgri_asset_init();
    CHECK(wgr_asset_set_cache_dir(ASYNC_CACHE));
    wgr_asset_set_host("https://assets.example.com/game");

    /* answered from another thread */
    CHECK(test_assets_set_fetcher(threaded_fetcher, NULL));
    remove(ASYNC_CACHE "/textures/rock.png");
    fetch_calls = 0;
    tasks[0] = wgr_asset_ensure("textures/rock.png", NULL, WGR_ASSET_NONE);
    test_assets_tick();
    CHECK(fetch_calls == 1);
    wgri_thread_join(&async_thread); /* the answer is in; it counts from the next tick */
    CHECK(wgr_asset_task_get_status(tasks[0]) == WGR_ASSET_TASK_PENDING);
    for (int i = 0; i < 4 && wgr_asset_task_get_status(tasks[0]) == WGR_ASSET_TASK_PENDING; i++) test_assets_tick();
    CHECK(wgr_asset_task_get_status(tasks[0]) == WGR_ASSET_TASK_DONE);
    CHECK(wgr_asset_task_destroy(tasks[0]));
    CHECK(read_back(ASYNC_CACHE "/textures/rock.png", "fetched"));

    /* six out at once; the rest wait for a slot */
    CHECK(test_assets_set_fetcher(holding_fetcher, NULL));
    held_count = 0;
    for (int n = 0; n < 10; n++) {
        char cached[160];
        snprintf(key, sizeof(key), "many/%d.bin", n);
        snprintf(cached, sizeof(cached), ASYNC_CACHE "/%s", key);
        remove(cached); /* a run before this one's: each has to be downloaded */
        tasks[n] = wgr_asset_ensure(key, NULL, WGR_ASSET_NONE);
    }
    for (int i = 0; i < 3; i++) test_assets_tick();
    CHECK(held_count == 6);
    CHECK(wgr_asset_fetch_done(held[0], true) && wgr_asset_fetch_done(held[1], true));
    for (int i = 0; i < 3; i++) test_assets_tick();
    CHECK(held_count == 8 && count_done(tasks, 10) == 2); /* two answered: two more out */
    for (int n = 2; n < 8; n++) wgr_asset_fetch_done(held[n], true);
    for (int i = 0; i < 3; i++) test_assets_tick();
    CHECK(held_count == 10);
    for (int n = 8; n < 10; n++) wgr_asset_fetch_done(held[n], true);
    for (int i = 0; i < 3; i++) test_assets_tick();
    CHECK(count_done(tasks, 10) == 10 && wgri_asset_pending_count() == 0);
    for (int n = 0; n < 10; n++) wgr_asset_task_destroy(tasks[n]);

    test_assets_set_fetcher(NULL, NULL);
    wgr_asset_set_host("");
    wgri_asset_deinit();
    CHECK(!wgr_asset_fetch_done(held[0], true)); /* after shutdown: turned away, not a crash */
    wgri_fs_deinit();
}
