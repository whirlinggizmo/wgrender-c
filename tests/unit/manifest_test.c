/* The asset manifest's pieces (docs/PLAN-asset-cache.md): the content hash, and the
 * reader for one directory's manifest.json. */
#include <stdlib.h>
#include <string.h>

#include "internal/wgr_manifest_internal.h"
#include "internal/wgr_sha256_internal.h"
#include "test.h"
#include "tests.h"

static bool hash_is(const char *text, size_t size, const char *expected)
{
    char out[WGRI_SHA256_TEXT];
    wgri_sha256_text((const unsigned char *)text, size, out);
    return strcmp(out + 7, expected) == 0 && strncmp(out, "sha256:", 7) == 0;
}

void test_sha256(void)
{
    /* FIPS 180-4's examples, and the lengths either side of a second padding block */
    CHECK(hash_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(hash_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(hash_is("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK(hash_is("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
                  112, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
    CHECK(hash_is("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 55,
                  "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    CHECK(hash_is("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 64,
                  "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));

    /* a million a's: many blocks */
    char *million = malloc(1000000);
    CHECK(million != NULL);
    if (million != NULL) {
        memset(million, 'a', 1000000);
        CHECK(hash_is(million, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
        free(million);
    }
}

#define HA "sha256:9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"
#define HB "sha256:3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b"

static bool parses(const char *json)
{
    wgri_manifest_t m;
    const bool ok = wgri_manifest_parse(json, strlen(json), &m);
    wgri_manifest_free(&m);
    return ok;
}

void test_manifest_parse(void)
{
    wgri_manifest_t m;
    const char *json = "{\n  \"wgr_manifest\": 1,\n"
                       "  \"files\": { \"tiles.png\": \"" HA "\", \"a \\\"quoted\\\" \\u00e9 name\": \"" HB "\",\n"
                       "             \"textures\": \"" HB "\" },\n"
                       "  \"dirs\": { \"textures\": \"" HA "\" },\n"
                       "  \"generator\": { \"by\": [\"gen_manifest.py\", 1.5e3, true, null] }\n}\n";

    CHECK(wgri_manifest_parse(json, strlen(json), &m));
    CHECK(m.count == 4);
    CHECK(wgri_manifest_find(&m, "tiles.png", false) != NULL &&
          strcmp(wgri_manifest_find(&m, "tiles.png", false), HA) == 0);
    CHECK(wgri_manifest_find(&m, "a \"quoted\" \xc3\xa9 name", false) != NULL); /* escapes decoded */
    /* a file and a directory may share a name; each is found as what it is */
    CHECK(strcmp(wgri_manifest_find(&m, "textures", false), HB) == 0);
    CHECK(strcmp(wgri_manifest_find(&m, "textures", true), HA) == 0);
    CHECK(wgri_manifest_find(&m, "tiles.png", true) == NULL);
    CHECK(wgri_manifest_find(&m, "missing.png", false) == NULL);
    wgri_manifest_free(&m);
    CHECK(m.entries == NULL && m.count == 0);

    /* the smallest one, and an empty one */
    CHECK(parses("{\"wgr_manifest\":1}"));
    CHECK(parses("{\"wgr_manifest\":1,\"files\":{},\"dirs\":{}}"));
    CHECK(parses("{\"wgr_manifest\":1,\"files\":{\"\\ud83d\\ude00.png\":\"" HA "\"}}")); /* a surrogate pair */

    /* anything that isn't exactly a manifest is none */
    CHECK(!parses(""));
    CHECK(!parses("[]"));
    CHECK(!parses("{}"));                                       /* no version */
    CHECK(!parses("{\"wgr_manifest\":2}"));                      /* another version */
    CHECK(!parses("{\"wgr_manifest\":1.0}"));
    CHECK(!parses("{\"wgr_manifest\":\"1\"}"));
    CHECK(!parses("{\"wgr_manifest\":1"));                       /* cut short */
    CHECK(!parses("{\"wgr_manifest\":1}{}"));                    /* something after it */
    CHECK(!parses("{\"wgr_manifest\":1,}"));                     /* a trailing comma */
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a\":\"sha256:abc\"}}"));   /* a short hash */
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a\":\"" "SHA256:9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08" "\"}}"));
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a\":\"sha256:9F86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\"}}"));
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a/b\":\"" HA "\"}}"));     /* not a name */
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"..\":\"" HA "\"}}"));
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"\":\"" HA "\"}}"));
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a\\u0000\":\"" HA "\"}}"));
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"\\ud83d.png\":\"" HA "\"}}")); /* half a pair */
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a\\q\":\"" HA "\"}}"));        /* no such escape */
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a\":\"" HA "\",\"a\":\"" HB "\"}}")); /* twice */
    CHECK(!parses("{\"wgr_manifest\":1,\"files\":{\"a\":1}}"));
    CHECK(!parses("{\"wgr_manifest\":1,\"other\":[1,2}"));        /* a broken unknown member */
    CHECK(!parses("{\"wgr_manifest\":1,\"other\":01}"));
}
