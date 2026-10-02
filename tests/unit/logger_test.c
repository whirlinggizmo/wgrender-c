/* The logger: its exported calls take finished text, and the C macros format in the
 * caller, only when the level lets the message through. */
#include "internal/wgr_internal_internal.h"
#include "wgr_logger.h"
#include "test.h"
#include "tests.h"

static int formatted;

static int count_formatting(void)
{
    return ++formatted;
}

void test_logger_level(void)
{
    const wgr_log_level_t was = wgr_logger_get_level();

    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(wgr_logger_get_level() == WGR_LOGGER_LEVEL_ERROR);

    /* a message the level drops isn't formatted: its arguments aren't evaluated */
    formatted = 0;
    wgr_logger_info("dropped %d", count_formatting());
    CHECK(formatted == 0);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL + 1); /* above everything: nothing is written */
    wgr_logger_fatal("passed the level %d", count_formatting());
    CHECK(formatted == 0);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_TRACE);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);
    wgr_logger_fatal("kept %d", count_formatting());
    CHECK(formatted == 1);

    wgr_logger_set_level(was);
}

void test_logger_utf8_cut(void)
{
    /* whole text, ASCII or not, is kept as it is */
    CHECK(wgri_logger_whole_utf8("plain") == 5);
    CHECK(wgri_logger_whole_utf8("caf\xC3\xA9") == 5);               /* é: 2 bytes */
    CHECK(wgri_logger_whole_utf8("\xE2\x82\xAC") == 3);              /* €: 3 bytes */
    CHECK(wgri_logger_whole_utf8("\xF0\x9F\x98\x80") == 4);          /* 😀: 4 bytes */
    CHECK(wgri_logger_whole_utf8("") == 0);
    /* a character cut short at the end goes, whole */
    CHECK(wgri_logger_whole_utf8("caf\xC3") == 3);
    CHECK(wgri_logger_whole_utf8("x\xE2\x82") == 1);
    CHECK(wgri_logger_whole_utf8("x\xF0\x9F\x98") == 1);
    CHECK(wgri_logger_whole_utf8("x\xF0") == 1);
}
