/* The local filesystem layer (src/sk_fs.c, docs/PLAN-sk_fs.md): where paths resolve,
 * reading and writing files, and creating the directories a write needs. The web half
 * (MEMFS + IndexedDB) isn't in this build; the desktop stubs for it are checked here so
 * callers can rely on their answers. */
#include <stdio.h>
#include <string.h>

#include "internal/sk_fs.h"
#include "test.h"
#include "tests.h"

#define ROOT "../build/test-fs"

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
    return sk_fs_write(path, (const unsigned char *)text, (int)strlen(text));
}

/* Text a file holds, or NULL; the buffer is NUL-terminated, so it compares as a
 * string, and `*size` is the byte count without the terminator. */
static char *read_text(const char *path, int *size)
{
    unsigned char *data = NULL;
    return sk_fs_read(path, &data, size) ? (char *)data : NULL;
}

void test_fs_paths(void)
{
    char out[512];

    sk_fs_init(NULL);
    CHECK(sk_fs_is_ready()); /* desktop: always */

    /* the root is joined with one separator, however it's given */
    sk_fs_set_root("/tmp/libsk-root");
    sk_fs_resolve("a/b.txt", out, sizeof(out));
    CHECK(strcmp(out, "/tmp/libsk-root/a/b.txt") == 0);
    sk_fs_set_root("/tmp/libsk-root/"); /* a trailing slash doesn't double it */
    sk_fs_resolve("a/b.txt", out, sizeof(out));
    CHECK(strcmp(out, "/tmp/libsk-root/a/b.txt") == 0);

    /* an absolute path is already where it says it is */
    sk_fs_resolve("/etc/hosts", out, sizeof(out));
    CHECK(strcmp(out, "/etc/hosts") == 0);

    /* no root: the path as it stands, relative to the working directory */
    sk_fs_set_root("");
    sk_fs_resolve("a/b.txt", out, sizeof(out));
    CHECK(strcmp(out, "a/b.txt") == 0);

    /* a path too long for the buffer is cut, not overrun */
    char small[8];
    sk_fs_set_root("/a/very/long/root");
    sk_fs_resolve("and/a/long/path.txt", small, sizeof(small));
    CHECK(strlen(small) == sizeof(small) - 1);

    sk_fs_deinit();
}

void test_fs_files(void)
{
    int size = 0;
    char *text;

    sk_fs_init(NULL);
    sk_fs_set_root(ROOT);
    remove_tree(); /* a previous run's files */

    CHECK(!sk_fs_exists("plain.txt"));
    CHECK(read_text("plain.txt", &size) == NULL); /* reading what isn't there fails */

    CHECK(write_text("plain.txt", "hello libsk"));
    CHECK(sk_fs_exists("plain.txt"));
    text = read_text("plain.txt", &size);
    CHECK(text != NULL && size == 11 && strcmp(text, "hello libsk") == 0);
    sk_fs_read_free((unsigned char *)text);

    /* a write makes the directories above it */
    CHECK(!sk_fs_exists("nested/deep/file.txt"));
    CHECK(write_text("nested/deep/file.txt", "deep"));
    CHECK(sk_fs_exists("nested/deep/file.txt"));
    text = read_text("nested/deep/file.txt", &size);
    CHECK(text != NULL && size == 4 && strcmp(text, "deep") == 0);
    sk_fs_read_free((unsigned char *)text);

    /* writing again replaces what was there */
    CHECK(write_text("plain.txt", "second"));
    text = read_text("plain.txt", &size);
    CHECK(text != NULL && size == 6 && strcmp(text, "second") == 0);
    sk_fs_read_free((unsigned char *)text);

    /* an empty file exists and reads as no bytes */
    CHECK(sk_fs_write("empty.bin", (const unsigned char *)"", 0));
    CHECK(sk_fs_exists("empty.bin"));
    text = read_text("empty.bin", &size);
    CHECK(text != NULL && size == 0 && text[0] == '\0');
    sk_fs_read_free((unsigned char *)text);

    /* the root moves, so the same relative path is a different file */
    sk_fs_set_root(ROOT "/nested");
    CHECK(!sk_fs_exists("plain.txt"));
    CHECK(sk_fs_exists("deep/file.txt"));
    sk_fs_set_root(ROOT);

    /* the cache is the web's; on desktop a file is either local or it isn't */
    CHECK(!sk_fs_is_cached("plain.txt"));
    CHECK(sk_fs_cache_read_begin("plain.txt") == 0);
    CHECK(sk_fs_cache_read_poll(1) == -1);

    remove_tree();
    sk_fs_deinit();
}
