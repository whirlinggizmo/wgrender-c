#include <string.h>

#include "internal/wgr_asset.h"
#include "test.h"
#include "tests.h"

static void check_join(const char *base, const char *uri, const char *expected)
{
    char out[256];
    const bool ok = wgr_asset_join_relative(base, uri, out, sizeof(out));
    CHECK(ok == (expected != NULL));
    if (ok && expected != NULL && strcmp(out, expected) != 0) {
        fprintf(stderr, "    join(%s, %s): got %s, expected %s\n", base, uri, out, expected);
        wgr_test_failures++;
    }
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
    CHECK(!wgr_asset_join_relative("models/box.gltf", "texture.png", small, sizeof(small))); /* doesn't fit */

    CHECK(wgr_asset_is_relative_uri("box.bin"));
    CHECK(wgr_asset_is_relative_uri("../textures/a.png"));
    CHECK(wgr_asset_is_relative_uri("dir/with:colon.png")); /* a colon after a slash isn't a scheme */
    CHECK(!wgr_asset_is_relative_uri("data:application/octet-stream;base64,AAAA"));
    CHECK(!wgr_asset_is_relative_uri("https://example.com/box.bin"));
    CHECK(!wgr_asset_is_relative_uri("/abs/box.bin"));
    CHECK(!wgr_asset_is_relative_uri(""));
    CHECK(!wgr_asset_is_relative_uri(NULL));
}
