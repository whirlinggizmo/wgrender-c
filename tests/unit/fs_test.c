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
    remove(ROOT "/.meta/nested/deep/file.txt");
    remove(ROOT "/.meta/plain.txt");
    remove(ROOT "/.meta/nested/deep");
    remove(ROOT "/.meta/nested");
    remove(ROOT "/.meta");
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

static wgri_fs_meta_t make_meta(const char *etag, double fresh_until, const char *hash)
{
    wgri_fs_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    snprintf(meta.etag, sizeof(meta.etag), "%s", etag);
    snprintf(meta.last_modified, sizeof(meta.last_modified), "Fri, 25 Sep 2026 12:00:00 GMT");
    meta.fresh_until = fresh_until;
    snprintf(meta.hash, sizeof(meta.hash), "%s", hash);
    return meta;
}

/* A cached file's metadata (docs/PLAN-asset-cache.md): kept with the bytes it
 * describes, replaced or dropped with them, and never outliving them. */
void test_fs_meta(void)
{
    static const char hash_a[] = "sha256:9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    static const char hash_b[] = "sha256:3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b";
    wgri_fs_meta_t meta = make_meta("\"v1\"", 1790000000.5, hash_a);
    wgri_fs_meta_t got;
    FILE *f;

    wgri_fs_init(NULL);
    wgri_fs_set_root(ROOT);
    remove_tree();

    /* no file, no metadata; and none can be set for a file that isn't there */
    CHECK(!wgri_fs_meta_get("plain.txt", &got));
    CHECK(got.etag[0] == '\0' && got.hash[0] == '\0' && got.fresh_until == 0.0);
    CHECK(!wgri_fs_meta_set("plain.txt", &meta));

    /* written together, read back exactly */
    CHECK(wgri_fs_write_meta("plain.txt", (const unsigned char *)"one", 3, &meta));
    CHECK(wgri_fs_meta_get("plain.txt", &got));
    CHECK(strcmp(got.etag, "\"v1\"") == 0);
    CHECK(strcmp(got.last_modified, "Fri, 25 Sep 2026 12:00:00 GMT") == 0);
    CHECK(got.fresh_until == 1790000000.5);
    CHECK(strcmp(got.hash, hash_a) == 0);

    /* a 304: the metadata changes, the bytes don't */
    meta = make_meta("W/\"v1-gzip\"", 1790000600.0, hash_a);
    CHECK(wgri_fs_meta_set("plain.txt", &meta));
    CHECK(wgri_fs_meta_get("plain.txt", &got) && strcmp(got.etag, "W/\"v1-gzip\"") == 0);
    CHECK(got.fresh_until == 1790000600.0);

    /* a plain write replaces the bytes, so what described the old ones goes */
    CHECK(write_text("plain.txt", "two"));
    CHECK(!wgri_fs_meta_get("plain.txt", &got));

    /* new bytes with their own metadata, in a directory of their own */
    meta = make_meta("\"v2\"", 0.0, hash_b);
    CHECK(wgri_fs_write_meta("nested/deep/file.txt", (const unsigned char *)"deep", 4, &meta));
    CHECK(wgri_fs_meta_get("nested/deep/file.txt", &got) && strcmp(got.hash, hash_b) == 0);
    CHECK(got.fresh_until == 0.0);

    /* the sidecar isn't beside the file: that name could be an asset's */
    CHECK(!wgri_fs_exists("nested/deep/file.txt.meta"));
    CHECK(wgri_fs_exists(".meta/nested/deep/file.txt"));

    /* a value too long for its field reads as none, never a cut one */
    f = fopen(ROOT "/.meta/nested/deep/file.txt", "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        char etag[300];
        memset(etag, 'x', sizeof(etag) - 1);
        etag[sizeof(etag) - 1] = '\0';
        fprintf(f, "wgr_meta 1\r\netag %s\r\nhash %s\r\n", etag, hash_b);
        fclose(f);
    }
    CHECK(wgri_fs_meta_get("nested/deep/file.txt", &got));
    CHECK(got.etag[0] == '\0' && strcmp(got.hash, hash_b) == 0);

    /* a sidecar that isn't one is none */
    f = fopen(ROOT "/.meta/nested/deep/file.txt", "wb");
    if (f != NULL) {
        fputs("etag \"v2\"\n", f);
        fclose(f);
    }
    CHECK(!wgri_fs_meta_get("nested/deep/file.txt", &got) && got.etag[0] == '\0');

    /* removing the file removes its metadata; one left behind doesn't count */
    CHECK(wgri_fs_meta_set("nested/deep/file.txt", &meta));
    CHECK(wgri_fs_remove("nested/deep/file.txt"));
    CHECK(!wgri_fs_exists(".meta/nested/deep/file.txt"));
    CHECK(wgri_fs_meta_set("plain.txt", &meta));
    remove(ROOT "/plain.txt");
    CHECK(!wgri_fs_meta_get("plain.txt", &got));

    /* an absolute path isn't the cache's */
    CHECK(!wgri_fs_meta_get(ROOT "/plain.txt", &got));

    remove_tree();
    wgri_fs_deinit();
}
