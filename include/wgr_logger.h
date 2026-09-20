#ifndef WGR_LOGGER_H
#define WGR_LOGGER_H

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

void wgr_logger_set_level(wgr_log_level_t level);
void wgr_logger_message(wgr_log_level_t level, const char *format, ...);
void wgr_logger_message_source(wgr_log_level_t level,
                              const char *source_file,
                              int source_line,
                              const char *format,
                              ...);

#define wgr_logger_trace(...) \
    wgr_logger_message_source(WGR_LOGGER_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define wgr_logger_debug(...) \
    wgr_logger_message_source(WGR_LOGGER_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define wgr_logger_info(...) \
    wgr_logger_message_source(WGR_LOGGER_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define wgr_logger_warn(...) \
    wgr_logger_message_source(WGR_LOGGER_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define wgr_logger_error(...) \
    wgr_logger_message_source(WGR_LOGGER_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define wgr_logger_fatal(...) \
    wgr_logger_message_source(WGR_LOGGER_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifndef WGR_NO_LOG_SHORT_MACROS
#define log_trace(...) wgr_logger_trace(__VA_ARGS__)
#define log_debug(...) wgr_logger_debug(__VA_ARGS__)
#define log_info(...) wgr_logger_info(__VA_ARGS__)
#define log_warn(...) wgr_logger_warn(__VA_ARGS__)
#define log_error(...) wgr_logger_error(__VA_ARGS__)
#define log_fatal(...) wgr_logger_fatal(__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif

#endif // WGR_LOGGER_H
