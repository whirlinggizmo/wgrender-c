#ifndef WGR_LOGGER_H
#define WGR_LOGGER_H

#include <stdio.h> /* snprintf, for the macros */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wgr_log_level_t
{
    WGR_LOGGER_LEVEL_TRACE = 0,
    WGR_LOGGER_LEVEL_DEBUG = 1,
    WGR_LOGGER_LEVEL_INFO = 2,
    WGR_LOGGER_LEVEL_WARN = 3,
    WGR_LOGGER_LEVEL_ERROR = 4,
    WGR_LOGGER_LEVEL_FATAL = 5
} wgr_log_level_t;

/* Messages at `level` and above are written (to stderr: the browser console on the web);
 * the rest are dropped. WGR_LOGGER_LEVEL_INFO by default. */
void wgr_logger_set_level(wgr_log_level_t level);
wgr_log_level_t wgr_logger_get_level(void);

/* Write a message: finished text, UTF-8, one line. A binding formats in its own
 * language and calls these. _source adds where it came from ("file.c:42: ..."). */
void wgr_logger_message(wgr_log_level_t level, const char *text);
void wgr_logger_message_source(wgr_log_level_t level,
                              const char *source_file,
                              int source_line,
                              const char *text);

/* For C: printf-style messages, formatted in the caller into 2 KB (a longer one is cut,
 * never inside a UTF-8 character) and not formatted at all when the level drops them,
 * with the caller's file and line: wgr_logger_info("loaded %s", path). Statements, not
 * expressions. */
#define wgr_logger_printf(level, ...)                                                    \
    do {                                                                                  \
        if ((level) >= wgr_logger_get_level()) {                                         \
            char wgr_logger_text_[2048];                                                 \
            snprintf(wgr_logger_text_, sizeof(wgr_logger_text_), __VA_ARGS__);           \
            wgr_logger_message_source((level), __FILE__, __LINE__, wgr_logger_text_);    \
        }                                                                                 \
    } while (0)
#define wgr_logger_trace(...) wgr_logger_printf(WGR_LOGGER_LEVEL_TRACE, __VA_ARGS__)
#define wgr_logger_debug(...) wgr_logger_printf(WGR_LOGGER_LEVEL_DEBUG, __VA_ARGS__)
#define wgr_logger_info(...) wgr_logger_printf(WGR_LOGGER_LEVEL_INFO, __VA_ARGS__)
#define wgr_logger_warn(...) wgr_logger_printf(WGR_LOGGER_LEVEL_WARN, __VA_ARGS__)
#define wgr_logger_error(...) wgr_logger_printf(WGR_LOGGER_LEVEL_ERROR, __VA_ARGS__)
#define wgr_logger_fatal(...) wgr_logger_printf(WGR_LOGGER_LEVEL_FATAL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // WGR_LOGGER_H
