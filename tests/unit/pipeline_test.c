/* Loading pipeline (docs/PLAN-pipeline.md), on sokol's dummy backend. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include "internal/sk_asset.h"
#include "internal/sk_environment.h"
#include "internal/sk_fs.h"
#include "internal/sk_internal.h"
#include "internal/sk_light.h"
#include "internal/sk_material.h"
#include "internal/sk_model.h"
#include "internal/sk_scene.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_texture.h"
#include "sk_asset.h"
#include "sk_audio.h"
#include "sk_logger.h"
#include "sk_model.h"
#include "sk_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

/* More textures than sokol's default pools hold load with libsk's pool sizes, and
 * running out fails the create instead of returning a texture with no image. */
void test_pipeline_gpu_pools(void)
{
    static const unsigned char pixel[4] = {255, 255, 255, 255};
    static sk_handle_t textures[600];
    int created = 0;

    sg_setup(&(sg_desc){.environment = sk_platform_environment(),
                        .buffer_pool_size = SK_GFX_BUFFER_POOL_SIZE,
                        .image_pool_size = SK_GFX_IMAGE_POOL_SIZE,
                        .view_pool_size = SK_GFX_VIEW_POOL_SIZE});
    sk_texture_init();
    for (int i = 0; i < 600; i++) {
        textures[i] = sk_texture_create_rgba(pixel, 1, 1);
        created += textures[i] != 0;
    }
    CHECK(created == 600);
    sk_texture_deinit();
    sg_shutdown();

    sg_setup(&(sg_desc){.environment = sk_platform_environment(), .image_pool_size = 8});
    sk_texture_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* sokol and libsk report the exhaustion */
    created = 0;
    for (int i = 0; i < 16; i++) {
        textures[i] = sk_texture_create_rgba(pixel, 1, 1);
        created += textures[i] != 0;
        if (textures[i] != 0) {
            CHECK(sk_texture_get_size(textures[i]).x == 1.0f);
        }
    }
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    CHECK(created > 0 && created < 16);
    sk_texture_deinit();
    sg_shutdown();
}

static void setup(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_audio_init();
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_light_init();
    sk_material_init();
    sk_environment_init();
    sk_model_init();
}

static void teardown(void)
{
    sk_audio_deinit();
    sk_model_deinit();
    sk_environment_deinit();
    sk_material_deinit();
    sk_light_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

#define GUMSHOE "../examples/assets/models/gumshoe/gumshoe.glb"

/* Textures a mesh's materials use that loaded (not missing, not the placeholder). */
static int loaded_textures(sk_handle_t mesh)
{
    int count = 0;
    for (int m = 0; m < sk_mesh_get_material_count(mesh); m++) {
        const sk_material_t *material = sk_material_get(sk_mesh_get_material(mesh, m));
        for (int t = 0; material != NULL && t < SK_MATERIAL_TEXTURE_COUNT; t++) {
            const sk_handle_t texture = material->textures[t].texture;
            count += texture != 0 && texture != sk_texture_get_placeholder() && sk_texture_get_size(texture).x > 1.0f;
        }
    }
    return count;
}

/* A .glb's embedded images decode (their bytes live in the file's buffer). */
void test_pipeline_mesh_textures(void)
{
    setup();
    sk_handle_t mesh = sk_mesh_create(GUMSHOE);
    CHECK(mesh != 0);
    CHECK(loaded_textures(mesh) >= 2);
    CHECK(sk_mesh_create(GUMSHOE) == mesh); /* deduped */
    sk_mesh_release(mesh);
    sk_mesh_release(mesh);
    teardown();
}

/* ------------------------------------------------------ async loading ---- */

#define ASSETS "../examples/assets"
#define TEXTURE "textures/blobshadow.png"

static struct {
    int successes, failures;
    sk_handle_t texture, mesh, audio, group_texture;
    char path[512];
    bool destroy_in_callback; /* create, then drop it again */
} got;

static void on_texture(const char *path, void *user)
{
    (void)user;
    got.successes++;
    snprintf(got.path, sizeof(got.path), "%s", path);
    got.texture = sk_texture_create(path);
    if (got.destroy_in_callback) sk_texture_release(got.texture);
}

static void on_mesh(const char *path, void *user)
{
    (void)user;
    got.successes++;
    got.mesh = sk_mesh_create(path);
}

static void on_audio(const char *path, void *user)
{
    (void)user;
    got.successes++;
    got.audio = sk_audio_create(path);
}

static void on_nothing(const char *path, void *user)
{
    (void)path;
    (void)user;
    got.successes++;
}

static void on_failed(const char *path, void *user)
{
    (void)path;
    (void)user;
    got.failures++;
}

static void start_assets(int workers, const char *host)
{
    setup();
    sk_fs_init(NULL);
    sk_asset_set_worker_count(workers);
    sk_asset_init();
    sk_asset_set_host(host);
    memset(&got, 0, sizeof(got));
}

static void stop_assets(void)
{
    sk_asset_deinit();
    sk_asset_set_worker_count(-1);
    sk_fs_deinit();
    teardown();
}

static void load(const char *path, unsigned int flags, sk_asset_callback_fn on_success)
{
    CHECK(sk_asset_add_task(sk_asset_ensure_async(path, NULL, flags), on_success, on_failed, NULL) ==
          SK_ASSET_ADD_TASK_OK);
}

/* Tick until nothing is pending; the frames it took, or -1 after 2000 frames. */
static int run_until_done(void)
{
    for (int frame = 1; frame <= 2000; frame++) {
        sk_asset_tick();
        if (sk_asset_pending_count() == 0) return frame;
        if (sk_asset_get_worker_count() > 0) {
            struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return -1;
}

/* A handle's texture is gone (stale handles warn; quiet here). */
static bool texture_freed(sk_handle_t texture)
{
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    const bool freed = sk_texture_get_size(texture).x == 0.0f;
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    return freed;
}

static void check_async_loads(int workers)
{
    start_assets(workers, ASSETS);
    CHECK(sk_asset_get_worker_count() == workers);
    load(TEXTURE, SK_ASSET_NONE, on_texture);
    load("models/gumshoe/gumshoe.glb", SK_ASSET_NONE, on_mesh);
    load("sounds/click_004.ogg", SK_ASSET_NONE, on_audio);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 3 && got.failures == 0);
    /* the callbacks' creates return the prepared resources, with one reference */
    CHECK(sk_texture_get_size(got.texture).x > 1.0f);
    CHECK(sk_texture_create(got.path) == got.texture);
    sk_texture_release(got.texture);
    CHECK(got.mesh != 0 && loaded_textures(got.mesh) >= 2);
    CHECK(got.audio != 0);
    sk_texture_release(got.texture);
    CHECK(texture_freed(got.texture)); /* the last reference is gone */
    sk_mesh_release(got.mesh);
    sk_audio_release(got.audio);
    stop_assets();
}

/* Files load before their callbacks, with and without worker threads. */
void test_pipeline_async(void)
{
    check_async_loads(0);
    check_async_loads(2);
}

/* A resource the callback doesn't create (or releases again) is freed. */
void test_pipeline_unclaimed(void)
{
    start_assets(1, ASSETS);
    got.destroy_in_callback = true;
    load(TEXTURE, SK_ASSET_NONE, on_texture);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1);
    CHECK(texture_freed(got.texture)); /* freed after the callback */

    /* a sync create while the file is being prepared: the callback gets the same one */
    got.destroy_in_callback = false;
    load(TEXTURE, SK_ASSET_NONE, on_texture);
    char local[512];
    sk_fs_resolve(TEXTURE, local, sizeof(local));
    const sk_handle_t texture = sk_texture_create(local);
    CHECK(run_until_done() > 0);
    CHECK(got.texture == texture);
    sk_texture_release(texture);
    sk_texture_release(texture);
    CHECK(texture_freed(texture));
    stop_assets();
}

/* A directory for test files (its parent exists); fine when it's already there. */
static bool make_dir(const char *dir)
{
#if defined(_WIN32)
    return _mkdir(dir) == 0 || errno == EEXIST;
#else
    return mkdir(dir, 0755) == 0 || errno == EEXIST;
#endif
}

/* Files that can't be loaded fire the failure callback; FILE_ONLY skips loading. */
void test_pipeline_failures(void)
{
    const char *dir = "build/pipeline_test";
    char path[256];
    FILE *f;

    CHECK(make_dir(dir));
    snprintf(path, sizeof(path), "%s/broken.png", dir);
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs("not a png", f);
    fclose(f);

    start_assets(1, dir);
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL);
    load("broken.png", SK_ASSET_NONE, on_nothing);
    load("missing.png", SK_ASSET_NONE, on_nothing);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 0 && got.failures == 2);
    load("broken.png", SK_ASSET_FILE_ONLY, on_nothing);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1 && got.failures == 2);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    stop_assets();
}

/* A mesh finishes over several frames (buffers, then one texture each) when the
 * budget is used up by each step. */
void test_pipeline_budget(void)
{
    start_assets(0, ASSETS);
    sk_asset_set_upload_budget(0.0f);
    load("models/gumshoe/gumshoe.glb", SK_ASSET_NONE, on_mesh);
    const int frames = run_until_done();
    /* frame 1 prepares and uploads the buffers; the 2 textures and the materials
     * take a frame each */
    CHECK(frames == 4);
    CHECK(got.mesh != 0 && loaded_textures(got.mesh) >= 2);
    sk_mesh_release(got.mesh);
    sk_asset_set_upload_budget(4.0f);
    stop_assets();
}

/* Shutting down with loads queued, running and half finished leaks and crashes
 * nothing (run under SANITIZE=address and thread). */
void test_pipeline_shutdown(void)
{
    for (int round = 0; round < 3; round++) {
        start_assets(2, ASSETS);
        sk_asset_set_upload_budget(0.0f);
        load("models/gumshoe/gumshoe.glb", SK_ASSET_NONE, on_mesh);
        load(TEXTURE, SK_ASSET_NONE, on_texture);
        load("sounds/click_004.ogg", SK_ASSET_NONE, on_audio);
        for (int frame = 0; frame < round * 3; frame++) {
            sk_asset_tick();
        }
        if (got.mesh != 0) sk_mesh_release(got.mesh);
        if (got.texture != 0) sk_texture_release(got.texture);
        if (got.audio != 0) sk_audio_release(got.audio);
        sk_asset_set_upload_budget(4.0f);
        stop_assets();
    }
}

static void on_group_done(const char *path, void *user)
{
    CHECK(path != NULL && path[0] == '\0');
    (*(int *)user)++;
}

static void on_group_create(const char *path, void *user)
{
    (void)path;
    (*(int *)user)++;
    got.group_texture = sk_texture_create(got.path);
}

/* A group completes after its members, fails if one does, and reports progress. */
void test_pipeline_group(void)
{
    int group_ok = 0, group_failed = 0;

    start_assets(1, ASSETS);
    sk_handle_t group = sk_asset_group_create();
    sk_handle_t texture = sk_asset_ensure_async(TEXTURE, NULL, SK_ASSET_NONE);
    sk_handle_t mesh = sk_asset_ensure_async("models/gumshoe/gumshoe.glb", NULL, SK_ASSET_NONE);
    CHECK(sk_asset_add_task(texture, on_texture, on_failed, NULL) == SK_ASSET_ADD_TASK_OK);
    CHECK(sk_asset_group_add(group, texture));
    CHECK(sk_asset_group_add(group, mesh)); /* no callbacks of its own */
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL);
    CHECK(!sk_asset_group_add(group, texture)); /* already in a group */
    CHECK(!sk_asset_group_add(group, group));
    CHECK(!sk_asset_group_add(texture, mesh));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    CHECK(sk_asset_add_task(group, on_group_done, on_group_done, &group_ok) == SK_ASSET_ADD_TASK_OK);
    CHECK(sk_asset_get_progress(group) == 0.0f);

    float last = 0.0f;
    bool monotonic = true;
    for (int frame = 0; frame < 2000 && sk_asset_pending_count() > 0; frame++) {
        sk_asset_tick();
        const float progress = sk_asset_get_progress(group);
        monotonic = monotonic && progress >= last && progress <= 1.0f;
        last = progress;
        struct timespec pause = {0, 1000000};
        nanosleep(&pause, NULL);
    }
    CHECK(monotonic);
    CHECK(group_ok == 1 && got.successes == 1 && got.failures == 0);
    CHECK(sk_asset_get_progress(group) == 1.0f); /* completed */
    sk_texture_release(got.texture);

    /* a missing member fails the group; the other members still load */
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL);
    group = sk_asset_group_create();
    CHECK(sk_asset_group_add(group, sk_asset_ensure_async("missing.png", NULL, SK_ASSET_NONE)));
    CHECK(sk_asset_group_add(group, sk_asset_ensure_async(TEXTURE, NULL, SK_ASSET_NONE)));
    CHECK(sk_asset_add_task(group, on_group_done, on_group_done, &group_failed) == SK_ASSET_ADD_TASK_OK);
    CHECK(run_until_done() > 0);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    CHECK(group_failed == 1);

    /* the group holds its members' resources for its own callback */
    group_ok = 0;
    group = sk_asset_group_create();
    got.destroy_in_callback = true; /* the member's callback takes the handle and drops it again */
    texture = sk_asset_ensure_async(TEXTURE, NULL, SK_ASSET_NONE);
    CHECK(sk_asset_add_task(texture, on_texture, on_failed, NULL) == SK_ASSET_ADD_TASK_OK);
    CHECK(sk_asset_group_add(group, texture));
    CHECK(sk_asset_add_task(group, on_group_create, on_failed, &group_ok) == SK_ASSET_ADD_TASK_OK);
    CHECK(run_until_done() > 0);
    CHECK(group_ok == 1);
    CHECK(got.group_texture == got.texture); /* the same resource, not a reload */
    sk_texture_release(got.group_texture);
    CHECK(texture_freed(got.group_texture));

    /* an empty group completes on the next tick */
    group_ok = 0;
    group = sk_asset_group_create();
    CHECK(sk_asset_add_task(group, on_group_done, NULL, &group_ok) == SK_ASSET_ADD_TASK_OK);
    CHECK(run_until_done() == 1);
    CHECK(group_ok == 1);
    stop_assets();
}

static int chained;

/* Each success queues another load from inside its callback, while tasks are in flight. */
static void on_chain(const char *path, void *user)
{
    (void)path;
    (void)user;
    got.successes++;
    if (chained < 200) {
        chained++;
        load(TEXTURE, SK_ASSET_NONE, on_chain);
    }
}

/* Far more loads in flight than the task pool starts with (and than the old fixed
 * 256), some queued from completion callbacks while the pool grows under them: all
 * of them complete. */
void test_pipeline_many(void)
{
    enum { LOADS = 600, CHAINS = 20 };

    start_assets(2, ASSETS);
    chained = 0;
    /* loaded once up front: every task then finds it, so this tests the task
     * bookkeeping rather than decoding the same file hundreds of times */
    const sk_handle_t texture = sk_texture_create(ASSETS "/" TEXTURE);
    CHECK(texture != 0);
    for (int i = 0; i < LOADS; i++) {
        load(TEXTURE, SK_ASSET_NONE, i < CHAINS ? on_chain : on_nothing);
    }
    CHECK(sk_asset_pending_count() == LOADS);
    CHECK(run_until_done() > 0);
    CHECK(got.failures == 0);
    CHECK(got.successes == LOADS + chained);
    CHECK(chained == 200);
    sk_texture_release(texture);
    stop_assets();
}

/* ------------------------------------------------- compressed glTF textures ---- */

/* A one-triangle model whose texture has the SK_texture_ktx extension, written by the
 * test beside copies of the flame texture (tex.png and its .ktx variants). */
#define KTX_DIR "build/ktx_model"
static const char KTX_GLTF[] =
    "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"SK_texture_ktx\"],"
    "\"buffers\":[{\"byteLength\":60,\"uri\":\"data:application/octet-stream;base64,"
    "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":24}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}],"
    "\"images\":[{\"uri\":\"tex.png\"},{\"uri\":\"tex.ktx\"}],"
    "\"textures\":[{\"source\":0,\"extensions\":{\"SK_texture_ktx\":{\"source\":1}}}],"
    "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"material\":0}]}],"
    "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

static bool copy_file(const char *from, const char *to)
{
    size_t size = 0;
    unsigned char *bytes = NULL;
    FILE *f = fopen(from, "rb");
    bool ok = false;
    if (f == NULL) return false;
    fseek(f, 0, SEEK_END);
    size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    bytes = malloc(size);
    if (bytes != NULL && fread(bytes, 1, size, f) == size) {
        FILE *out = fopen(to, "wb");
        ok = out != NULL && fwrite(bytes, 1, size, out) == size;
        if (out != NULL) fclose(out);
    }
    fclose(f);
    free(bytes);
    return ok;
}

static struct {
    char uris[8][256];
    char fallbacks[8][256];
    int count;
} listed;

static void on_dependency(const char *uri, const char *fallback_uri, bool required, void *context)
{
    (void)required, (void)context;
    if (listed.count < 8) {
        snprintf(listed.fallbacks[listed.count], sizeof(listed.fallbacks[0]), "%s", fallback_uri ? fallback_uri : "");
        snprintf(listed.uris[listed.count++], sizeof(listed.uris[0]), "%s", uri);
    }
}

/* The fallback listed with `uri` ("" for none). */
static const char *listed_fallback(const char *uri)
{
    for (int i = 0; i < listed.count; i++) {
        if (strcmp(listed.uris[i], uri) == 0) return listed.fallbacks[i];
    }
    return "";
}

static bool was_listed(const char *uri)
{
    for (int i = 0; i < listed.count; i++) {
        if (strcmp(listed.uris[i], uri) == 0) return true;
    }
    return false;
}

void test_pipeline_gltf_ktx(void)
{
    static const char *variants[] = {".png", ".bc7.ktx", ".astc.ktx", ".etc2.ktx"};
    char from[256], to[256];
    FILE *f;

    CHECK(make_dir("build") && make_dir(KTX_DIR));
    for (int i = 0; i < 4; i++) {
        snprintf(from, sizeof(from), "../examples/assets/textures/flame%s", variants[i]);
        snprintf(to, sizeof(to), KTX_DIR "/tex%s", variants[i]);
        CHECK(copy_file(from, to));
    }
    f = fopen(KTX_DIR "/m.gltf", "wb");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs(KTX_GLTF, f);
    fclose(f);

    /* what it needs: the compressed file this GPU can use, not the PNG; the PNG when
       there's none */
    sk_texture_set_ktx_support(1); /* BC7 */
    listed.count = 0;
    sk_model_list_gltf_dependencies((const unsigned char *)KTX_GLTF, (int)strlen(KTX_GLTF), on_dependency, NULL);
    CHECK(was_listed("tex.bc7.ktx") && !was_listed("tex.png") && !was_listed("tex.ktx"));
    CHECK(strcmp(listed_fallback("tex.bc7.ktx"), "tex.png") == 0); /* if the compressed file is missing */
    sk_texture_set_ktx_support(2); /* ASTC */
    listed.count = 0;
    sk_model_list_gltf_dependencies((const unsigned char *)KTX_GLTF, (int)strlen(KTX_GLTF), on_dependency, NULL);
    CHECK(was_listed("tex.astc.ktx") && !was_listed("tex.png"));
    sk_texture_set_ktx_support(0); /* none */
    listed.count = 0;
    sk_model_list_gltf_dependencies((const unsigned char *)KTX_GLTF, (int)strlen(KTX_GLTF), on_dependency, NULL);
    CHECK(was_listed("tex.png") && !was_listed("tex.ktx") && !was_listed("tex.bc7.ktx") && !was_listed("tex.astc.ktx"));
    sk_texture_set_ktx_support(-1);

    /* loaded on sokol's dummy backend (no compressed formats): the texture's own image */
    setup();
    sk_handle_t mesh = sk_mesh_create(KTX_DIR "/m.gltf");
    CHECK(mesh != 0);
    CHECK(loaded_textures(mesh) == 1);
    sk_mesh_release(mesh);
    teardown();
}

/* A compressed texture whose variant for this GPU is missing loads its PNG instead,
 * through the asset layer and through sk_texture_create; a variant named outright
 * has no fallback. */
void test_pipeline_ktx_fallback(void)
{
    CHECK(make_dir("build") && make_dir(KTX_DIR));
    CHECK(copy_file("../examples/assets/textures/flame.png", KTX_DIR "/png_only.png"));
    remove(KTX_DIR "/png_only.bc7.ktx");
    start_assets(0, KTX_DIR);
    sk_texture_set_ktx_support(1); /* BC7, which png_only doesn't have */
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR); /* the fallback warns */
    load("png_only.ktx", SK_ASSET_NONE, on_texture);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1 && got.failures == 0);
    CHECK(strstr(got.path, "png_only.png") != NULL);
    CHECK(sk_texture_get_size(got.texture).x == 256.0f);
    const sk_handle_t sync = sk_texture_create(KTX_DIR "/png_only.ktx");
    CHECK(sync == got.texture); /* the same file: deduped */
    sk_texture_release(sync);
    sk_texture_release(got.texture);

    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* the failure logs an error */
    load("png_only.bc7.ktx", SK_ASSET_NONE, on_texture);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1 && got.failures == 1);
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_texture_set_ktx_support(-1);
    stop_assets();
}
