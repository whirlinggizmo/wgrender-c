/* Optional subsystems register themselves when linked (internal/wgr_module.h): the test
 * binary links them all, so all are there, in init order; a module added later takes
 * its place by order, and the frame callbacks reach it. */
#include <string.h>

#include "internal/wgr_module_internal.h"
#include "test.h"
#include "tests.h"

static int flushed, ended;
static void count_flush(void) { flushed++; }
static void count_end_frame(void) { ended++; }

void test_module_registry(void)
{
    static const char *expected[] = {"gamepad", "texture", "light", "material", "environment", "model", "sprite_batch",
                                     "sprite3d", "sprite2d", "emitter", "text2d", "text3d", "audio", "sound"};
    static wgr_module_t probe = {.name = "probe", .order = 15, .flush = count_flush, .end_frame = count_end_frame};
    int found = 0, previous_order = -1000, position = 0, probe_at = -1, light_at = -1, texture_at = -1;

    wgr_module_register(&probe);
    for (const wgr_module_t *m = wgr_module_list(); m != NULL; m = m->next, position++) {
        CHECK(m->order >= previous_order); /* in init order */
        previous_order = m->order;
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            found += strcmp(m->name, expected[i]) == 0 ? 1 : 0;
        }
        if (strcmp(m->name, "probe") == 0) probe_at = position;
        if (strcmp(m->name, "light") == 0) light_at = position;
        if (strcmp(m->name, "texture") == 0) texture_at = position;
    }
    CHECK(found == (int)(sizeof(expected) / sizeof(expected[0])));
    CHECK(texture_at < probe_at && probe_at < light_at); /* order 10 < 15 < 20 */

    wgr_module_flush_all();
    wgr_module_end_frame_all();
    CHECK(flushed == 1 && ended == 1);
}
