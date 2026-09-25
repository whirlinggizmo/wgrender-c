/* Loading pipeline (docs/PLAN-pipeline.md), on sokol's dummy backend. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include "internal/wgr_asset_internal.h"
#include "internal/wgr_environment_internal.h"
#include "internal/wgr_fs_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_light_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_model_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_asset.h"
#include "wgr_pick.h"
#include "wgr_light.h"
#include "wgr_scene.h"
#include "wgr_render.h"
#include "sokol_time.h"
#include "wgr_audio.h"
#include "wgr_logger.h"
#include "wgr_model.h"
#include "wgr_texture.h"
#include "test.h"
#include "test_os.h"
#include "tests.h"

#include "sokol_gfx.h"

/* More textures than sokol's default pools hold load with libwgrender's pool sizes, and
 * running out fails the create instead of returning a texture with no image. */
void test_pipeline_gpu_pools(void)
{
    static const unsigned char pixel[4] = {255, 255, 255, 255};
    static wgr_handle_t textures[600];
    int created = 0;

    sg_setup(&(sg_desc){.environment = wgri_platform_environment(),
                        .buffer_pool_size = WGRI_GFX_BUFFER_POOL_SIZE,
                        .image_pool_size = WGRI_GFX_IMAGE_POOL_SIZE,
                        .view_pool_size = WGRI_GFX_VIEW_POOL_SIZE});
    wgri_texture_init();
    for (int i = 0; i < 600; i++) {
        textures[i] = wgri_texture_create_rgba(pixel, 1, 1);
        created += textures[i] != 0;
    }
    CHECK(created == 600);
    wgri_texture_deinit();
    sg_shutdown();

    sg_setup(&(sg_desc){.environment = wgri_platform_environment(), .image_pool_size = 8});
    wgri_texture_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* sokol and libwgrender report the exhaustion */
    created = 0;
    for (int i = 0; i < 16; i++) {
        textures[i] = wgri_texture_create_rgba(pixel, 1, 1);
        created += textures[i] != 0;
        if (textures[i] != 0) {
            CHECK(wgr_texture_get_size(textures[i]).x == 1.0f);
        }
    }
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    CHECK(created > 0 && created < 16);
    wgri_texture_deinit();
    sg_shutdown();
}

static void setup(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_audio_init();
    wgri_render_init();
    wgri_scene_init();
    wgri_camera3d_init();
    wgri_texture_init();
    wgri_light_init();
    wgri_material_init();
    wgri_environment_init();
    wgri_model_init();
}

static void teardown(void)
{
    wgri_audio_deinit();
    wgri_model_deinit();
    wgri_environment_deinit();
    wgri_material_deinit();
    wgri_light_deinit();
    wgri_texture_deinit();
    wgri_camera3d_deinit();
    wgri_scene_deinit();
    wgri_render_deinit();
    sg_shutdown();
}

#define CHARACTER "examples/assets/" CHARACTER_PATH

/* Textures a mesh's materials use that loaded (not missing, not the placeholder). */
static int loaded_textures(wgr_handle_t mesh)
{
    int count = 0;
    for (int m = 0; m < wgr_mesh_get_material_count(mesh); m++) {
        const wgri_material_t *material = wgri_material_get(wgr_mesh_get_material(mesh, m));
        for (int t = 0; material != NULL && t < WGRI_MATERIAL_TEXTURE_COUNT; t++) {
            const wgr_handle_t texture = material->textures[t].texture;
            count += texture != 0 && texture != wgr_texture_get_placeholder() && wgr_texture_get_size(texture).x > 1.0f;
        }
    }
    return count;
}

/* A .glb's embedded images decode (their bytes live in the file's buffer). */
void test_pipeline_mesh_textures(void)
{
    setup();
    wgr_handle_t mesh = wgr_mesh_create(CHARACTER);
    CHECK(mesh != 0);
    CHECK(loaded_textures(mesh) >= 2);
    CHECK(wgr_mesh_create(CHARACTER) == mesh); /* deduped */
    wgr_mesh_release(mesh);
    wgr_mesh_release(mesh);
    teardown();
}

/* ------------------------------------------------------ async loading ---- */

#define ASSETS "examples/assets"
#define TEXTURE "textures/blobshadow.png"

static struct {
    int successes, failures;
    wgr_handle_t texture, mesh, audio, group_texture;
    char path[512];
    bool destroy_in_callback; /* create, then drop it again */
} got;

static void on_texture(const char *path, void *user)
{
    (void)user;
    got.successes++;
    snprintf(got.path, sizeof(got.path), "%s", path);
    got.texture = wgr_texture_create(path);
    if (got.destroy_in_callback) wgr_texture_release(got.texture);
}

static void on_mesh(const char *path, void *user)
{
    (void)user;
    got.successes++;
    got.mesh = wgr_mesh_create(path);
}

static void on_audio(const char *path, void *user)
{
    (void)user;
    got.successes++;
    got.audio = wgr_audio_create(path);
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
    wgri_fs_init(NULL);
    wgri_asset_set_worker_count(workers);
    wgri_asset_init();
    wgr_asset_set_host(host);
    memset(&got, 0, sizeof(got));
}

static void stop_assets(void)
{
    wgri_asset_deinit();
    wgri_asset_set_worker_count(-1);
    wgri_fs_deinit();
    teardown();
}

static void load(const char *path, unsigned int flags, wgr_asset_callback_fn on_success)
{
    CHECK(wgr_asset_add_task(wgr_asset_ensure_async(path, NULL, flags), on_success, on_failed, NULL) ==
          WGR_ASSET_ADD_TASK_OK);
}

/* Tick until nothing is pending; the frames it took, or -1 after 2000 frames. */
static int run_until_done(void)
{
    for (int frame = 1; frame <= 2000; frame++) {
        wgri_asset_tick();
        if (wgri_asset_pending_count() == 0) return frame;
        if (wgri_asset_get_worker_count() > 0) {
            test_sleep_ms(1);
        }
    }
    return -1;
}

/* A handle's texture is gone (stale handles warn; quiet here). */
static bool texture_freed(wgr_handle_t texture)
{
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    const bool freed = wgr_texture_get_size(texture).x == 0.0f;
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    return freed;
}

static void check_async_loads(int workers)
{
    start_assets(workers, ASSETS);
    CHECK(wgri_asset_get_worker_count() == workers);
    load(TEXTURE, WGR_ASSET_NONE, on_texture);
    load(CHARACTER_PATH, WGR_ASSET_NONE, on_mesh);
    load("sounds/click_004.ogg", WGR_ASSET_NONE, on_audio);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 3 && got.failures == 0);
    /* the callbacks' creates return the prepared resources, with one reference */
    CHECK(wgr_texture_get_size(got.texture).x > 1.0f);
    CHECK(wgr_texture_create(got.path) == got.texture);
    wgr_texture_release(got.texture);
    CHECK(got.mesh != 0 && loaded_textures(got.mesh) >= 2);
    CHECK(got.audio != 0);
    wgr_texture_release(got.texture);
    CHECK(texture_freed(got.texture)); /* the last reference is gone */
    wgr_mesh_release(got.mesh);
    wgr_audio_release(got.audio);
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
    load(TEXTURE, WGR_ASSET_NONE, on_texture);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1);
    CHECK(texture_freed(got.texture)); /* freed after the callback */

    /* a sync create while the file is being prepared: the callback gets the same one */
    got.destroy_in_callback = false;
    load(TEXTURE, WGR_ASSET_NONE, on_texture);
    char local[512];
    wgri_fs_resolve(TEXTURE, local, sizeof(local));
    const wgr_handle_t texture = wgr_texture_create(local);
    CHECK(run_until_done() > 0);
    CHECK(got.texture == texture);
    wgr_texture_release(texture);
    wgr_texture_release(texture);
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
    const char *dir = WGR_TEST_DIR "/pipeline_test";
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
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);
    load("broken.png", WGR_ASSET_NONE, on_nothing);
    load("missing.png", WGR_ASSET_NONE, on_nothing);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 0 && got.failures == 2);
    load("broken.png", WGR_ASSET_FILE_ONLY, on_nothing);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1 && got.failures == 2);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    stop_assets();
}

/* A mesh finishes over several frames (buffers, then one texture each) when the
 * budget is used up by each step. */
void test_pipeline_budget(void)
{
    start_assets(0, ASSETS);
    wgr_asset_set_upload_budget(0.0f);
    load(CHARACTER_PATH, WGR_ASSET_NONE, on_mesh);
    const int frames = run_until_done();
    /* frame 1 prepares and uploads the buffers; the 2 textures and the materials
     * take a frame each */
    CHECK(frames == 4);
    CHECK(got.mesh != 0 && loaded_textures(got.mesh) >= 2);
    wgr_mesh_release(got.mesh);
    wgr_asset_set_upload_budget(4.0f);
    stop_assets();
}

/* Shutting down with loads queued, running and half finished leaks and crashes
 * nothing (run under SANITIZE=address and thread). */
void test_pipeline_shutdown(void)
{
    for (int round = 0; round < 3; round++) {
        start_assets(2, ASSETS);
        wgr_asset_set_upload_budget(0.0f);
        load(CHARACTER_PATH, WGR_ASSET_NONE, on_mesh);
        load(TEXTURE, WGR_ASSET_NONE, on_texture);
        load("sounds/click_004.ogg", WGR_ASSET_NONE, on_audio);
        for (int frame = 0; frame < round * 3; frame++) {
            wgri_asset_tick();
        }
        if (got.mesh != 0) wgr_mesh_release(got.mesh);
        if (got.texture != 0) wgr_texture_release(got.texture);
        if (got.audio != 0) wgr_audio_release(got.audio);
        wgr_asset_set_upload_budget(4.0f);
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
    got.group_texture = wgr_texture_create(got.path);
}

/* A group completes after its members, fails if one does, and reports progress. */
void test_pipeline_group(void)
{
    int group_ok = 0, group_failed = 0;

    start_assets(1, ASSETS);
    wgr_handle_t group = wgr_asset_group_create();
    wgr_handle_t texture = wgr_asset_ensure_async(TEXTURE, NULL, WGR_ASSET_NONE);
    wgr_handle_t mesh = wgr_asset_ensure_async(CHARACTER_PATH, NULL, WGR_ASSET_NONE);
    CHECK(wgr_asset_add_task(texture, on_texture, on_failed, NULL) == WGR_ASSET_ADD_TASK_OK);
    CHECK(wgr_asset_group_add(group, texture));
    CHECK(wgr_asset_group_add(group, mesh)); /* no callbacks of its own */
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);
    CHECK(!wgr_asset_group_add(group, texture)); /* already in a group */
    CHECK(!wgr_asset_group_add(group, group));
    CHECK(!wgr_asset_group_add(texture, mesh));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    CHECK(wgr_asset_add_task(group, on_group_done, on_group_done, &group_ok) == WGR_ASSET_ADD_TASK_OK);
    CHECK(wgr_asset_get_progress(group) == 0.0f);

    float last = 0.0f;
    bool monotonic = true;
    for (int frame = 0; frame < 2000 && wgri_asset_pending_count() > 0; frame++) {
        wgri_asset_tick();
        const float progress = wgr_asset_get_progress(group);
        monotonic = monotonic && progress >= last && progress <= 1.0f;
        last = progress;
        test_sleep_ms(1);
    }
    CHECK(monotonic);
    CHECK(group_ok == 1 && got.successes == 1 && got.failures == 0);
    CHECK(wgr_asset_get_progress(group) == 1.0f); /* completed */
    wgr_texture_release(got.texture);

    /* a missing member fails the group; the other members still load */
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);
    group = wgr_asset_group_create();
    CHECK(wgr_asset_group_add(group, wgr_asset_ensure_async("missing.png", NULL, WGR_ASSET_NONE)));
    CHECK(wgr_asset_group_add(group, wgr_asset_ensure_async(TEXTURE, NULL, WGR_ASSET_NONE)));
    CHECK(wgr_asset_add_task(group, on_group_done, on_group_done, &group_failed) == WGR_ASSET_ADD_TASK_OK);
    CHECK(run_until_done() > 0);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    CHECK(group_failed == 1);

    /* the group holds its members' resources for its own callback */
    group_ok = 0;
    group = wgr_asset_group_create();
    got.destroy_in_callback = true; /* the member's callback takes the handle and drops it again */
    texture = wgr_asset_ensure_async(TEXTURE, NULL, WGR_ASSET_NONE);
    CHECK(wgr_asset_add_task(texture, on_texture, on_failed, NULL) == WGR_ASSET_ADD_TASK_OK);
    CHECK(wgr_asset_group_add(group, texture));
    CHECK(wgr_asset_add_task(group, on_group_create, on_failed, &group_ok) == WGR_ASSET_ADD_TASK_OK);
    CHECK(run_until_done() > 0);
    CHECK(group_ok == 1);
    CHECK(got.group_texture == got.texture); /* the same resource, not a reload */
    wgr_texture_release(got.group_texture);
    CHECK(texture_freed(got.group_texture));

    /* an empty group completes on the next tick */
    group_ok = 0;
    group = wgr_asset_group_create();
    CHECK(wgr_asset_add_task(group, on_group_done, NULL, &group_ok) == WGR_ASSET_ADD_TASK_OK);
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
        load(TEXTURE, WGR_ASSET_NONE, on_chain);
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
    const wgr_handle_t texture = wgr_texture_create(ASSETS "/" TEXTURE);
    CHECK(texture != 0);
    for (int i = 0; i < LOADS; i++) {
        load(TEXTURE, WGR_ASSET_NONE, i < CHAINS ? on_chain : on_nothing);
    }
    CHECK(wgri_asset_pending_count() == LOADS);
    CHECK(run_until_done() > 0);
    CHECK(got.failures == 0);
    CHECK(got.successes == LOADS + chained);
    CHECK(chained == 200);
    wgr_texture_release(texture);
    stop_assets();
}

/* ------------------------------------------------- compressed glTF textures ---- */

/* A one-triangle model whose texture has the WGR_texture_ktx extension, written by the
 * test beside copies of the flame texture (tex.png and its .ktx variants). */
#define KTX_DIR WGR_TEST_DIR "/ktx_model"
static const char KTX_GLTF[] =
    "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"WGR_texture_ktx\"],"
    "\"buffers\":[{\"byteLength\":60,\"uri\":\"data:application/octet-stream;base64,"
    "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":24}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}],"
    "\"images\":[{\"uri\":\"tex.png\"},{\"uri\":\"tex.ktx\"}],"
    "\"textures\":[{\"source\":0,\"extensions\":{\"WGR_texture_ktx\":{\"source\":1}}}],"
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

    CHECK(make_dir(WGR_TEST_DIR) && make_dir(KTX_DIR));
    for (int i = 0; i < 4; i++) {
        snprintf(from, sizeof(from), "examples/assets/textures/flame%s", variants[i]);
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
    wgri_texture_set_ktx_support(1); /* BC7 */
    listed.count = 0;
    wgri_model_list_gltf_dependencies((const unsigned char *)KTX_GLTF, (int)strlen(KTX_GLTF), on_dependency, NULL);
    CHECK(was_listed("tex.bc7.ktx") && !was_listed("tex.png") && !was_listed("tex.ktx"));
    CHECK(strcmp(listed_fallback("tex.bc7.ktx"), "tex.png") == 0); /* if the compressed file is missing */
    wgri_texture_set_ktx_support(2); /* ASTC */
    listed.count = 0;
    wgri_model_list_gltf_dependencies((const unsigned char *)KTX_GLTF, (int)strlen(KTX_GLTF), on_dependency, NULL);
    CHECK(was_listed("tex.astc.ktx") && !was_listed("tex.png"));
    wgri_texture_set_ktx_support(0); /* none */
    listed.count = 0;
    wgri_model_list_gltf_dependencies((const unsigned char *)KTX_GLTF, (int)strlen(KTX_GLTF), on_dependency, NULL);
    CHECK(was_listed("tex.png") && !was_listed("tex.ktx") && !was_listed("tex.bc7.ktx") && !was_listed("tex.astc.ktx"));
    wgri_texture_set_ktx_support(-1);

    /* loaded on sokol's dummy backend (no compressed formats): the texture's own image */
    setup();
    wgr_handle_t mesh = wgr_mesh_create(KTX_DIR "/m.gltf");
    CHECK(mesh != 0);
    CHECK(loaded_textures(mesh) == 1);
    wgr_mesh_release(mesh);
    teardown();
}

/* A compressed texture whose variant for this GPU is missing loads its PNG instead,
 * through the asset layer and through wgr_texture_create; a variant named outright
 * has no fallback. */
void test_pipeline_ktx_fallback(void)
{
    CHECK(make_dir(WGR_TEST_DIR) && make_dir(KTX_DIR));
    CHECK(copy_file("examples/assets/textures/flame.png", KTX_DIR "/png_only.png"));
    remove(KTX_DIR "/png_only.bc7.ktx");
    start_assets(0, KTX_DIR);
    wgri_texture_set_ktx_support(1); /* BC7, which png_only doesn't have */
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR); /* the fallback warns */
    load("png_only.ktx", WGR_ASSET_NONE, on_texture);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1 && got.failures == 0);
    CHECK(strstr(got.path, "png_only.png") != NULL);
    CHECK(wgr_texture_get_size(got.texture).x == 256.0f);
    const wgr_handle_t sync = wgr_texture_create(KTX_DIR "/png_only.ktx");
    CHECK(sync == got.texture); /* the same file: deduped */
    wgr_texture_release(sync);
    wgr_texture_release(got.texture);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the failure logs an error */
    load("png_only.bc7.ktx", WGR_ASSET_NONE, on_texture);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1 && got.failures == 1);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_texture_set_ktx_support(-1);
    stop_assets();
}

/* ------------------------------------------------------------ redirects ---- */

#define REDIRECT_DIR WGR_TEST_DIR "/redirect"

static struct {
    int calls;
    float ms[4];
    char host[4][256];
} pinged;

static void on_ping(const char *host, float milliseconds, void *user)
{
    (void)user;
    if (pinged.calls < 4) {
        pinged.ms[pinged.calls] = milliseconds;
        snprintf(pinged.host[pinged.calls], sizeof(pinged.host[0]), "%s", host);
    }
    pinged.calls++;
}

/* Redirect rules stack (the newest first, then the file itself), a file missing under
 * a rule falls through, a model's files are found through the rules too, and a
 * download rule leaves desktop loading alone. */
void test_pipeline_redirects(void)
{
    static const char *dirs[] = {WGR_TEST_DIR, REDIRECT_DIR, REDIRECT_DIR "/textures", REDIRECT_DIR "/models",
                                 REDIRECT_DIR "/mods", REDIRECT_DIR "/mods/base", REDIRECT_DIR "/mods/base/textures",
                                 REDIRECT_DIR "/mods/top", REDIRECT_DIR "/mods/top/textures",
                                 REDIRECT_DIR "/mods/top/models"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) CHECK(make_dir(dirs[i]));
    /* flame.png is 256x256, noise.png 128x128: the size says which file loaded */
    CHECK(copy_file("examples/assets/textures/flame.png", REDIRECT_DIR "/textures/only_base.png"));
    CHECK(copy_file("examples/assets/textures/flame.png", REDIRECT_DIR "/textures/both.png"));
    CHECK(copy_file("examples/assets/textures/noise.png", REDIRECT_DIR "/mods/base/textures/both.png"));
    CHECK(copy_file("examples/assets/textures/noise.png", REDIRECT_DIR "/mods/top/textures/top.png"));
    CHECK(copy_file("examples/assets/textures/flame.png", REDIRECT_DIR "/textures/top.png"));
    CHECK(copy_file("examples/assets/textures/flame.png", REDIRECT_DIR "/models/tex.png"));
    CHECK(copy_file("examples/assets/textures/noise.png", REDIRECT_DIR "/mods/top/models/tex.png"));
    FILE *f = fopen(REDIRECT_DIR "/models/m.gltf", "wb");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs(KTX_GLTF, f); /* a triangle with tex.png (tex.ktx isn't used: no compressed formats here) */
    fclose(f);

    start_assets(0, REDIRECT_DIR);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(!wgr_asset_add_redirect("", "x/"));
    CHECK(wgr_asset_add_redirect("textures/", "mods/base/textures/"));
    CHECK(wgr_asset_add_redirect("textures/", "mods/top/textures/")); /* added last: tried first */
    CHECK(wgr_asset_add_redirect("models/", "mods/top/models/"));
    CHECK(wgr_asset_add_redirect("models/", "https://cdn.example.com/models/")); /* web only; ignored here */

    load("textures/only_base.png", WGR_ASSET_NONE, on_texture); /* in no mod: the file itself */
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 1 && strstr(got.path, "mods/") == NULL);
    CHECK(wgr_texture_get_size(got.texture).x == 256.0f);
    wgr_texture_release(got.texture);

    load("textures/both.png", WGR_ASSET_NONE, on_texture); /* only in base: top falls through to it */
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 2 && strstr(got.path, "mods/base/textures/both.png") != NULL);
    CHECK(wgr_texture_get_size(got.texture).x == 128.0f);
    wgr_texture_release(got.texture);

    load("textures/top.png", WGR_ASSET_NONE, on_texture); /* in top: wins over the file itself */
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 3 && strstr(got.path, "mods/top/textures/top.png") != NULL);
    wgr_texture_release(got.texture);

    load("textures/nowhere.png", WGR_ASSET_NONE, on_texture);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the failure logs an error */
    CHECK(run_until_done() > 0);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(got.successes == 3 && got.failures == 1);

    /* the model is only in models/, its image is overridden in the mod */
    load("models/m.gltf", WGR_ASSET_NONE, on_mesh);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 4 && got.mesh != 0);
    const wgri_material_t *material = wgri_material_get(wgr_mesh_get_material(got.mesh, 0));
    CHECK(material != NULL && wgr_texture_get_size(material->textures[WGRI_MATERIAL_TEXTURE_BASE_COLOR].texture).x == 128.0f);
    wgr_mesh_release(got.mesh);

    wgr_asset_clear_redirects();
    load("textures/top.png", WGR_ASSET_NONE, on_texture);
    CHECK(run_until_done() > 0);
    CHECK(got.successes == 5 && strstr(got.path, "mods/") == NULL);
    wgr_texture_release(got.texture);

    /* ping (desktop: the host is a directory) */
    memset(&pinged, 0, sizeof(pinged));
    CHECK(!wgr_asset_ping_host(NULL, 0, NULL, NULL));
    CHECK(wgr_asset_ping_host(NULL, 0, on_ping, NULL));
    CHECK(wgr_asset_ping_host(REDIRECT_DIR "/missing", 0, on_ping, NULL));
    CHECK(wgr_asset_ping_host("https://example.com", 0, on_ping, NULL));
    CHECK(pinged.calls == 0); /* on a later tick */
    wgri_asset_tick();
    CHECK(pinged.calls == 3);
    CHECK(pinged.ms[0] == 0.0f && strcmp(pinged.host[0], REDIRECT_DIR) == 0);
    CHECK(pinged.ms[1] < 0.0f && pinged.ms[2] < 0.0f);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    stop_assets();
}

/* ------------------------------------------------------- generated meshes ---- */

/* Generated meshes are resources like loaded ones: shared by parameters, with a default
 * material, picked like any model. */
void test_pipeline_generated_meshes(void)
{
    setup();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* bad sizes below log on purpose */
    const wgr_handle_t plane = wgr_mesh_create_plane(10, 10, 2);
    CHECK(plane != 0 && wgr_handle_get_kind(plane) == WGR_HANDLE_KIND_MESH);
    CHECK(wgr_mesh_create_plane(10, 10, 2) == plane); /* the same parameters: the same mesh */
    const wgr_handle_t other = wgr_mesh_create_plane(10, 10, 3);
    CHECK(other != 0 && other != plane);
    CHECK(wgr_mesh_create_plane(0, 10, 2) == 0);
    CHECK(wgr_mesh_get_material_count(plane) == 1);
    const wgri_material_t *material = wgri_material_get(wgr_mesh_get_material(plane, 0));
    CHECK(material != NULL && material->metallic == 0.0f && material->roughness == 0.5f);

    /* a ray straight down at the plane hits it, at its height */
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 1, 5, 1, 1, 0, 1.001f, 0, 0, -1);
    const wgr_handle_t floor = wgr_model_create(plane);
    wgr_model_set_transform(floor, 0, -1, 0, 0, 0, 0, 1, 1, 1);
    wgr_pick_result_t r = wgr_pick_object(floor, camera, 0.5f, 0.5f);
    CHECK(r.hit && r.handle == floor);
    CHECK_NEAR(r.point_world.y, -1.0f, 1e-3f);
    wgr_model_set_transform(floor, 20, -1, 0, 0, 0, 0, 1, 1, 1); /* moved away: missed */
    CHECK(!wgr_pick_object(floor, camera, 0.5f, 0.5f).hit);

    /* each shape makes a mesh */
    const wgr_handle_t shapes[] = {wgr_mesh_create_cube(1, 2, 3),      wgr_mesh_create_sphere(1, 16, 32),
                                  wgr_mesh_create_cylinder(1, 2, 16), wgr_mesh_create_cone(1, 2, 16),
                                  wgr_mesh_create_capsule(0.5f, 2, 8, 16), wgr_mesh_create_torus(1, 0.25f, 24, 12)};
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        CHECK(shapes[i] != 0);
        wgr_mesh_release(shapes[i]);
    }

    wgr_model_destroy(floor);
    wgr_mesh_release(plane);
    wgr_mesh_release(plane);
    wgr_mesh_release(other);
    wgr_camera3d_destroy(camera);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    teardown();
}

/* Skinned models draw through the frame's joint texture (src/wgr_model.c): several
 * models, each with its own pose, in one frame. The dummy backend validates the
 * uniform blocks and bindings; wgri_model_flush uploads the joints before the passes. */
void test_pipeline_skinned_joints(void)
{
    enum { MODELS = 3 };
    wgr_handle_t models[MODELS];

    setup();
    stm_setup(); /* the frame's time */
    const wgr_handle_t mesh = wgr_mesh_create(CHARACTER);
    CHECK(mesh != 0);
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 2, 8, 0, 1, 0, 0, 1, 0);
    const wgr_handle_t scene = wgr_scene_create();
    wgr_scene_set_active_camera(scene, camera);
    const wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_scene_add(scene, sun, 0);
    for (int i = 0; i < MODELS; i++) {
        models[i] = wgr_model_create(mesh);
        wgr_model_set_transform(models[i], (float)i * 2.0f - 2.0f, 0, 0, 0, 0, 0, 1, 1, 1);
        CHECK(wgr_model_set_animation(models[i], 0));
        wgr_model_animate(models[i], 0.1f * (float)(i + 1)); /* each in a different pose */
        wgr_scene_add(scene, models[i], 0);
    }
    for (int frame = 0; frame < 3; frame++) { /* the joints go up once a frame */
        for (int i = 0; i < MODELS; i++) wgr_model_animate(models[i], 1.0f / 60.0f);
        wgr_render_begin_frame();
        wgr_scene_draw(scene);
        wgr_render_end_frame();
    }
    for (int i = 0; i < MODELS; i++) wgr_model_destroy(models[i]);
    wgr_mesh_release(mesh);
    wgr_scene_destroy(scene);
    wgr_camera3d_destroy(camera);
    teardown();
}
