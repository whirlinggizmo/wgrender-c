/* The local filesystem layer (src/wgr_fs.c, docs/PLAN-wgr_fs.md): where paths resolve,
 * reading and writing files, and creating the directories a write needs. The web half
 * (MEMFS + IndexedDB) isn't in this build; the desktop stubs for it are checked here so
 * callers can rely on their answers. */
#include <stdio.h>
#include <string.h>

#include "internal/wgr_fs_internal.h"
#include "test.h"
#include "tests.h"

#define ROOT WGR_TEST_DIR "/fs"

static void remove_tree(void)
{
    remove(ROOT "/nested/deep/file.txt");
    remove(ROOT "/plain.txt");
    remove(ROOT "/empty.bin");
    remove(ROOT "/nested/deep");
    remove(ROOT "/nested");
    remove(ROOT);
}

static bool write_text(const char *path, const char *text)
{
    return wgri_fs_write(path, (const unsigned char *)text, (int)strlen(text));
}

/* Text a file holds, or NULL; the buffer is NUL-terminated, so it compares as a
 * string, and `*size` is the byte count without the terminator. */
static char *read_text(const char *path, int *size)
{
    unsigned char *data = NULL;
    return wgri_fs_read(path, &data, size) ? (char *)data : NULL;
}

void test_fs_paths(void)
{
    char out[512];

    wgri_fs_init(NULL);
    CHECK(wgri_fs_is_ready()); /* desktop: always */

    /* the root is joined with one separator, however it's given */
    wgri_fs_set_root("/tmp/libwgrender-root");
    wgri_fs_resolve("a/b.txt", out, sizeof(out));
    CHECK(strcmp(out, "/tmp/libwgrender-root/a/b.txt") == 0);
    wgri_fs_set_root("/tmp/libwgrender-root/"); /* a trailing slash doesn't double it */
    wgri_fs_resolve("a/b.txt", out, sizeof(out));
    CHECK(strcmp(out, "/tmp/libwgrender-root/a/b.txt") == 0);

    /* an absolute path is already where it says it is */
    wgri_fs_resolve("/etc/hosts", out, sizeof(out));
    CHECK(strcmp(out, "/etc/hosts") == 0);

    /* no root: the path as it stands, relative to the working directory */
    wgri_fs_set_root("");
    wgri_fs_resolve("a/b.txt", out, sizeof(out));
    CHECK(strcmp(out, "a/b.txt") == 0);

    /* a path too long for the buffer is cut, not overrun */
    char small[8];
    wgri_fs_set_root("/a/very/long/root");
    wgri_fs_resolve("and/a/long/path.txt", small, sizeof(small));
    CHECK(strlen(small) == sizeof(small) - 1);

    wgri_fs_deinit();
}

void test_fs_files(void)
{
    int size = 0;
    char *text;

    wgri_fs_init(NULL);
    wgri_fs_set_root(ROOT);
    remove_tree(); /* a previous run's files */

    CHECK(!wgri_fs_exists("plain.txt"));
    CHECK(read_text("plain.txt", &size) == NULL); /* reading what isn't there fails */

    CHECK(write_text("plain.txt", "hello libwgrender"));
    CHECK(wgri_fs_exists("plain.txt"));
    text = read_text("plain.txt", &size);
    CHECK(text != NULL && size == (int)strlen("hello libwgrender") && strcmp(text, "hello libwgrender") == 0);
    wgri_fs_read_free((unsigned char *)text);

    /* a write makes the directories above it */
    CHECK(!wgri_fs_exists("nested/deep/file.txt"));
    CHECK(write_text("nested/deep/file.txt", "deep"));
    CHECK(wgri_fs_exists("nested/deep/file.txt"));
    text = read_text("nested/deep/file.txt", &size);
    CHECK(text != NULL && size == 4 && strcmp(text, "deep") == 0);
    wgri_fs_read_free((unsigned char *)text);

    /* writing again replaces what was there */
    CHECK(write_text("plain.txt", "second"));
    text = read_text("plain.txt", &size);
    CHECK(text != NULL && size == 6 && strcmp(text, "second") == 0);
    wgri_fs_read_free((unsigned char *)text);

    /* an empty file exists and reads as no bytes */
    CHECK(wgri_fs_write("empty.bin", (const unsigned char *)"", 0));
    CHECK(wgri_fs_exists("empty.bin"));
    text = read_text("empty.bin", &size);
    CHECK(text != NULL && size == 0 && text[0] == '\0');
    wgri_fs_read_free((unsigned char *)text);

    /* the root moves, so the same relative path is a different file */
    wgri_fs_set_root(ROOT "/nested");
    CHECK(!wgri_fs_exists("plain.txt"));
    CHECK(wgri_fs_exists("deep/file.txt"));
    wgri_fs_set_root(ROOT);

    /* the cache is the web's; on desktop a file is either local or it isn't */
    CHECK(!wgri_fs_is_cached("plain.txt"));
    CHECK(wgri_fs_cache_read_begin("plain.txt") == 0);
    CHECK(wgri_fs_cache_read_poll(1) == -1);

    remove_tree();
    wgri_fs_deinit();
}
