#ifndef SK_LOGGER_H
#define SK_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sk_log_level_t
{
    SK_LOGGER_LEVEL_TRACE = 0,
    SK_LOGGER_LEVEL_DEBUG = 1,
    SK_LOGGER_LEVEL_INFO = 2,
    SK_LOGGER_LEVEL_WARN = 3,
    SK_LOGGER_LEVEL_ERROR = 4,
    SK_LOGGER_LEVEL_FATAL = 5
} sk_log_level_t;

void sk_logger_set_level(sk_log_level_t level);
void sk_logger_message(sk_log_level_t level, const char *format, ...);
void sk_logger_message_source(sk_log_level_t level,
                              const char *source_file,
                              int source_line,
                              const char *format,
                              ...);

#define sk_logger_trace(...) \
    sk_logger_message_source(SK_LOGGER_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define sk_logger_debug(...) \
    sk_logger_message_source(SK_LOGGER_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define sk_logger_info(...) \
    sk_logger_message_source(SK_LOGGER_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define sk_logger_warn(...) \
    sk_logger_message_source(SK_LOGGER_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define sk_logger_error(...) \
    sk_logger_message_source(SK_LOGGER_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define sk_logger_fatal(...) \
    sk_logger_message_source(SK_LOGGER_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifndef SK_NO_LOG_SHORT_MACROS
#define log_trace(...) sk_logger_trace(__VA_ARGS__)
#define log_debug(...) sk_logger_debug(__VA_ARGS__)
#define log_info(...) sk_logger_info(__VA_ARGS__)
#define log_warn(...) sk_logger_warn(__VA_ARGS__)
#define log_error(...) sk_logger_error(__VA_ARGS__)
#define log_fatal(...) sk_logger_fatal(__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif

#endif // SK_LOGGER_H
