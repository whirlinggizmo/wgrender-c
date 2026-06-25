#ifndef RL_LOGGER_H
#define RL_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WGUTILS_LOG_MACROS_DEFINED
#define WGUTILS_LOG_MACROS_DEFINED 1
#endif

typedef enum rl_log_level_t
{
    RL_LOGGER_LEVEL_TRACE = 0,
    RL_LOGGER_LEVEL_DEBUG = 1,
    RL_LOGGER_LEVEL_INFO = 2,
    RL_LOGGER_LEVEL_WARN = 3,
    RL_LOGGER_LEVEL_ERROR = 4,
    RL_LOGGER_LEVEL_FATAL = 5
} rl_log_level_t;

void rl_logger_set_level(rl_log_level_t level);
void rl_logger_message(rl_log_level_t level, const char *format, ...);
void rl_logger_message_source(rl_log_level_t level,
                              const char *source_file,
                              int source_line,
                              const char *format,
                              ...);

#define rl_logger_trace(...) \
    rl_logger_message_source(RL_LOGGER_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define rl_logger_debug(...) \
    rl_logger_message_source(RL_LOGGER_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define rl_logger_info(...) \
    rl_logger_message_source(RL_LOGGER_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define rl_logger_warn(...) \
    rl_logger_message_source(RL_LOGGER_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define rl_logger_error(...) \
    rl_logger_message_source(RL_LOGGER_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define rl_logger_fatal(...) \
    rl_logger_message_source(RL_LOGGER_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#define log_trace(...) rl_logger_trace(__VA_ARGS__)
#define log_debug(...) rl_logger_debug(__VA_ARGS__)
#define log_info(...) rl_logger_info(__VA_ARGS__)
#define log_warn(...) rl_logger_warn(__VA_ARGS__)
#define log_error(...) rl_logger_error(__VA_ARGS__)
#define log_fatal(...) rl_logger_fatal(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // RL_LOGGER_H
