/* Compressed textures (docs/PLAN-textures.md): the KTX parser on real files written by
 * tools/compress_textures.sh and on broken ones, the variant each GPU gets, and loading
 * through sk_texture_create as far as sokol's dummy backend allows (it samples no
 * compressed format). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/sk_ktx.h"
#include "internal/sk_platform.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"
#include "sk_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define FLAME "../examples/assets/textures/flame"

static unsigned char *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *bytes = NULL;
    long n;
    *size = 0;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && (bytes = malloc((size_t)n)) != NULL && fread(bytes, 1, (size_t)n, f) == (size_t)n) {
        *size = (size_t)n;
    }
    fclose(f);
    return bytes;
}

void test_ktx_parse(void)
{
    static const struct {
        const char *path;
        sg_pixel_format format;
    } files[] = {
        {FLAME ".bc7.ktx", SG_PIXELFORMAT_BC7_RGBA},
        {FLAME ".astc.ktx", SG_PIXELFORMAT_ASTC_4x4_RGBA},
        {FLAME ".etc2.ktx", SG_PIXELFORMAT_ETC2_RGBA8},
    };
    const char *error = NULL;
    sk_ktx_t ktx;
    size_t size;

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        unsigned char *bytes = read_all(files[i].path, &size);
        CHECK(bytes != NULL);
        if (bytes == NULL) return;
        CHECK(sk_ktx_parse(bytes, size, &ktx, &error));
        CHECK(ktx.format == files[i].format);
        CHECK(ktx.width == 256 && ktx.height == 256 && ktx.mip_count == 9); /* 256 .. 1 */
        CHECK(ktx.sizes[0] == 64 * 64 * 16 && ktx.sizes[8] == 16);     /* 4x4 blocks, 16 bytes */
        CHECK(ktx.levels[0] > bytes && ktx.levels[8] + ktx.sizes[8] <= bytes + size);
        free(bytes);
    }

    /* broken ones are refused, with a reason */
    unsigned char *bytes = read_all(FLAME ".bc7.ktx", &size);
    unsigned char *copy = malloc(size);
    CHECK(bytes != NULL && copy != NULL);
    if (bytes == NULL || copy == NULL) return;
    memcpy(copy, bytes, size);
    copy[1] = 'X'; /* identifier */
    CHECK(!sk_ktx_parse(copy, size, &ktx, &error) && strstr(error, "KTX") != NULL);
    memcpy(copy, bytes, size);
    copy[12 + 4 * 4] = 0x01; /* glInternalFormat: something else */
    CHECK(!sk_ktx_parse(copy, size, &ktx, &error));
    CHECK(!sk_ktx_parse(bytes, size - 100, &ktx, &error) && strstr(error, "truncated") != NULL);
    memcpy(copy, bytes, size);
    copy[12 + 6 * 4] = 0x80; /* pixelWidth 128: level 0's size no longer fits it */
    CHECK(!sk_ktx_parse(copy, size, &ktx, &error));
    CHECK(!sk_ktx_parse(bytes, 20, &ktx, &error));
    CHECK(!sk_ktx_parse(NULL, 0, &ktx, &error));
    free(copy);
    free(bytes);
}

void test_ktx_variants(void)
{
    char out[128];

    sk_texture_set_ktx_support(0x7); /* all three: BC7 first */
    CHECK(sk_texture_ktx_path("textures/rock.ktx", out, sizeof(out)) && strcmp(out, "textures/rock.bc7.ktx") == 0);
    sk_texture_set_ktx_support(0x6); /* a phone: ASTC and ETC2 */
    CHECK(sk_texture_ktx_path("textures/rock.ktx", out, sizeof(out)) && strcmp(out, "textures/rock.astc.ktx") == 0);
    sk_texture_set_ktx_support(0x4);
    CHECK(sk_texture_ktx_path("textures/rock.ktx", out, sizeof(out)) && strcmp(out, "textures/rock.etc2.ktx") == 0);
    sk_texture_set_ktx_support(0);   /* none: the PNG */
    CHECK(sk_texture_ktx_path("textures/rock.ktx", out, sizeof(out)) && strcmp(out, "textures/rock.png") == 0);

    /* a variant named outright is kept; other paths aren't ours */
    sk_texture_set_ktx_support(0x7);
    CHECK(sk_texture_ktx_path("a/rock.astc.ktx", out, sizeof(out)) && strcmp(out, "a/rock.astc.ktx") == 0);
    CHECK(!sk_texture_ktx_path("textures/rock.png", out, sizeof(out)));
    CHECK(!sk_texture_ktx_path(NULL, out, sizeof(out)));
    CHECK(!sk_texture_ktx_path("textures/a-very-long-name.ktx", out, 8)); /* doesn't fit */
    sk_texture_set_ktx_support(-1);
}

/* Loading through sk_texture_create. sokol's dummy backend (these tests) samples no
 * compressed format: a variant is refused with a reason, and the PNG loads when no
 * compressed format is usable. Real GPUs are checked by examples/textures.c. */
void test_ktx_load(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_texture_init();

    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL);
    sk_texture_set_ktx_support(1); /* pretend BC7 works: the file loads, the GPU refuses it */
    CHECK(sk_texture_create(FLAME ".ktx") == 0);
    CHECK(sk_texture_create("../examples/assets/textures/missing.ktx") == 0);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);

    /* no compressed format: rock.ktx loads rock.png */
    sk_texture_set_ktx_support(0);
    const sk_handle_t png = sk_texture_create(FLAME ".ktx");
    CHECK(png != 0 && sk_texture_get_size(png).x == 256.0f && sk_texture_get_size(png).y == 256.0f);
    CHECK(png == sk_texture_create(FLAME ".png")); /* the same texture */
    sk_texture_release(png);
    sk_texture_release(png);

    sk_texture_set_ktx_support(-1);
    sk_texture_deinit();
    sg_shutdown();
}
