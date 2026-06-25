#include "raylib.h"
#include "rl_logger.h"
#include "logger/logger.h"

#include <stdarg.h>
#include <stdio.h>

static int map_log_level_to_wgutils(rl_log_level_t level)
{
    switch (level) {
        case RL_LOGGER_LEVEL_TRACE:
            return LOG_LEVEL_TRACE;
        case RL_LOGGER_LEVEL_DEBUG:
            return LOG_LEVEL_DEBUG;
        case RL_LOGGER_LEVEL_INFO:
            return LOG_LEVEL_INFO;
        case RL_LOGGER_LEVEL_WARN:
            return LOG_LEVEL_WARN;
        case RL_LOGGER_LEVEL_ERROR:
            return LOG_LEVEL_ERROR;
        case RL_LOGGER_LEVEL_FATAL:
            return LOG_LEVEL_FATAL;
        default:
            return LOG_LEVEL_INFO;
    }
}

static void install_raylib_log_reroute(int log_level, const char *text, va_list args)
{
    char msg[2048];
    va_list copy;
    int level = LOG_LEVEL_INFO;

    va_copy(copy, args);
    vsnprintf(msg, sizeof(msg), text, copy);
    va_end(copy);

    switch (log_level) {
        case LOG_TRACE:
            level = LOG_LEVEL_TRACE;
            break;
        case LOG_DEBUG:
            level = LOG_LEVEL_DEBUG;
            break;
        case LOG_INFO:
            level = LOG_LEVEL_INFO;
            break;
        case LOG_WARNING:
            level = LOG_LEVEL_WARN;
            break;
        case LOG_ERROR:
            level = LOG_LEVEL_ERROR;
            break;
        case LOG_FATAL:
            level = LOG_LEVEL_FATAL;
            break;
        default:
            level = LOG_LEVEL_INFO;
            break;
    }

    log_message(level, "raylib", 0, "%s", msg);
}

void rl_logger_message(rl_log_level_t level, const char *format, ...)
{
    char message[2048];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(map_log_level_to_wgutils(level), "app", 0, "%s", message);
}

void rl_logger_message_source(rl_log_level_t level,
                              const char *source_file,
                              int source_line,
                              const char *format,
                              ...)
{
    char message[2048];
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_message(map_log_level_to_wgutils(level),
                source_file != NULL && source_file[0] != '\0' ? source_file : "app",
                source_line,
                "%s",
                message);
}

void rl_logger_set_level(rl_log_level_t level)
{
    int raylib_level = LOG_INFO;

    log_set_log_level((size_t)level);

    switch (level) {
        case RL_LOGGER_LEVEL_TRACE:
            raylib_level = LOG_TRACE;
            break;
        case RL_LOGGER_LEVEL_DEBUG:
            raylib_level = LOG_DEBUG;
            break;
        case RL_LOGGER_LEVEL_INFO:
            raylib_level = LOG_INFO;
            break;
        case RL_LOGGER_LEVEL_WARN:
            raylib_level = LOG_WARNING;
            break;
        case RL_LOGGER_LEVEL_ERROR:
            raylib_level = LOG_ERROR;
            break;
        case RL_LOGGER_LEVEL_FATAL:
            raylib_level = LOG_FATAL;
            break;
        default:
            raylib_level = LOG_INFO;
            break;
    }

    SetTraceLogLevel(raylib_level);
}

void rl_logger_init(void)
{
    SetTraceLogCallback(install_raylib_log_reroute);
}

void rl_logger_deinit(void)
{
    /* no-op: raylib does not provide a public API to restore internal default callback */
}
