#include "wgr_logger.h"

#include "internal/wgr_internal_internal.h"

#include <stdio.h>
#include <string.h>

static wgr_log_level_t wgr_log_level = WGR_LOGGER_LEVEL_INFO;

static const char *level_name(wgr_log_level_t level)
{
    switch (level) {
        case WGR_LOGGER_LEVEL_TRACE: return "TRACE";
        case WGR_LOGGER_LEVEL_DEBUG: return "DEBUG";
        case WGR_LOGGER_LEVEL_INFO:  return "INFO";
        case WGR_LOGGER_LEVEL_WARN:  return "WARN";
        case WGR_LOGGER_LEVEL_ERROR: return "ERROR";
        case WGR_LOGGER_LEVEL_FATAL: return "FATAL";
        default:                    return "INFO";
    }
}

static const char *basename_of(const char *path)
{
    const char *slash;
    if (path == NULL) {
        return "app";
    }
    slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\'); /* MSVC's __FILE__ is a Windows path */
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    return slash != NULL ? slash + 1 : path;
}

/* A cut sequence is dropped whole. */
size_t wgri_logger_whole_utf8(const char *text)
{
    size_t n = strlen(text), start = n;
    while (start > 0 && n - start < 3 && ((unsigned char)text[start - 1] & 0xC0) == 0x80) {
        start--; /* continuation bytes */
    }
    if (start > 0) {
        const unsigned char lead = (unsigned char)text[start - 1];
        const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (need > 1 && n - (start - 1) < need) {
            return start - 1; /* the lead byte and what arrived of its continuation */
        }
    }
    return n;
}

static void emit(wgr_log_level_t level, const char *source_file, int source_line, const char *text)
{
    if (level < wgr_log_level) {
        return;
    }
    if (text == NULL) {
        text = "";
    }
    const int length = (int)wgri_logger_whole_utf8(text);
    if (source_file != NULL && source_file[0] != '\0') {
        fprintf(stderr, "[%-5s] %s:%d: %.*s\n", level_name(level), basename_of(source_file), source_line, length, text);
    } else {
        fprintf(stderr, "[%-5s] %.*s\n", level_name(level), length, text);
    }
}

void wgr_logger_message(wgr_log_level_t level, const char *text)
{
    emit(level, NULL, 0, text);
}

void wgr_logger_message_source(wgr_log_level_t level, const char *source_file, int source_line, const char *text)
{
    emit(level, source_file, source_line, text);
}

wgr_log_level_t wgr_logger_get_level(void)
{
    return wgr_log_level;
}

void wgr_logger_set_level(wgr_log_level_t level)
{
    wgr_log_level = level;
}

void wgri_logger_init(void)
{
    /* nothing to set up for the stderr backend */
}

void wgri_logger_deinit(void)
{
    /* no-op */
}
