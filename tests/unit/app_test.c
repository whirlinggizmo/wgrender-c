#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/wgr_internal_internal.h"
#include "wgr.h"
#include "wgr_asset.h"

#include "test.h"
#include "tests.h"

static void check_clean(const char *name, const char *expected)
{
    char out[128];
    const bool ok = wgri_app_clean_name(name, out, sizeof(out));
    CHECK(ok == (expected != NULL));
    if (ok && expected != NULL && strcmp(out, expected) != 0) {
        fprintf(stderr, "    clean(%s): got %s, expected %s\n", name, out, expected);
        wgr_test_failures++;
    }
}

static void set_env(const char *name, const char *value)
{
#if defined(_WIN32)
    _putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

/* Who the program is, and the cache directory that follows from it. */
void test_app_identity(void)
{
    char dir[512], expected[512];

    /* one safe path component, whatever it was given */
    check_clean("Whirling Gizmo", "Whirling Gizmo");
    check_clean("My Game: Deluxe", "My Game_ Deluxe");
    check_clean("a/b\\c", "a_b_c");
    check_clean("<>\"|?*", "______");
    check_clean("  .hidden. ", "hidden");
    check_clean("..", NULL);
    check_clean("", NULL);
    check_clean(NULL, NULL);
    check_clean("CON", "_CON");
    check_clean("con.txt", "_con.txt");
    check_clean("COM1", "_COM1");
    check_clean("lpt9", "_lpt9");
    check_clean("COM0", "COM0"); /* not a device */
    check_clean("CONSOLE", "CONSOLE");

    CHECK(wgri_app_join("/home/u/.cache", "Co", "App", NULL, dir, sizeof(dir)) &&
          strcmp(dir, "/home/u/.cache/Co/App") == 0);
    CHECK(wgri_app_join("C:\\Users\\u\\AppData\\Local", "Co", "App", "cache", dir, sizeof(dir)) &&
          strcmp(dir, "C:/Users/u/AppData/Local/Co/App/cache") == 0);
    CHECK(!wgri_app_join("", "Co", "App", NULL, dir, sizeof(dir)));

    /* the defaults: nothing that looks like anyone's, and the executable's name */
    wgr_set_app_company(NULL);
    wgr_set_app_name(NULL);
    CHECK(strcmp(wgr_get_app_company(), "DefaultCompany") == 0);
    CHECK(strcmp(wgr_get_app_name(), "unit_tests") == 0);

    wgr_set_app_company("Acme/Games");
    wgr_set_app_name("Rocket");
    CHECK(strcmp(wgr_get_app_company(), "Acme_Games") == 0);
    CHECK(strcmp(wgr_get_app_name(), "Rocket") == 0);
    wgr_set_app_name("..."); /* nothing left: back to the default */
    CHECK(strcmp(wgr_get_app_name(), "unit_tests") == 0);
    wgr_set_app_name("Rocket");

#if defined(_WIN32)
    set_env("LOCALAPPDATA", "C:\\Users\\u\\AppData\\Local");
    snprintf(expected, sizeof(expected), "C:/Users/u/AppData/Local/Acme_Games/Rocket/cache");
#elif defined(__APPLE__)
    set_env("HOME", "/Users/u");
    snprintf(expected, sizeof(expected), "/Users/u/Library/Caches/Acme_Games/Rocket");
#else
    {
        const char *home = getenv("HOME");
        char saved_home[512];
        snprintf(saved_home, sizeof(saved_home), "%s", home != NULL ? home : "");
        set_env("XDG_CACHE_HOME", "relative/ignored"); /* the spec ignores a relative one */
        set_env("HOME", "/home/u");
        CHECK(wgri_app_cache_dir(dir, sizeof(dir)) && strcmp(dir, "/home/u/.cache/Acme_Games/Rocket") == 0);
        set_env("HOME", saved_home);
    }
    set_env("XDG_CACHE_HOME", "/var/cache/u");
    snprintf(expected, sizeof(expected), "/var/cache/u/Acme_Games/Rocket");
#endif
    CHECK(wgri_app_cache_dir(dir, sizeof(dir)));
    if (strcmp(dir, expected) != 0) {
        fprintf(stderr, "    cache dir: got %s, expected %s\n", dir, expected);
        wgr_test_failures++;
    }
#if !defined(_WIN32) && !defined(__APPLE__)
    set_env("XDG_CACHE_HOME", NULL);
#endif

    /* one the program sets wins, and is what the getter says */
    CHECK(wgr_asset_set_cache_dir(WGR_TEST_DIR "/app-cache"));
    CHECK(strcmp(wgr_asset_get_cache_dir(), WGR_TEST_DIR "/app-cache") == 0);

    wgr_set_app_company(NULL);
    wgr_set_app_name(NULL);
}
