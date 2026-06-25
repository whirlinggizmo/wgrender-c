#include "sk_logger.h"

#include "internal/sk_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static sk_log_level_t sk_log_level = SK_LOGGER_LEVEL_INFO;

static const char *level_name(sk_log_level_t level)
{
    switch (level) {
        case SK_LOGGER_LEVEL_TRACE: return "TRACE";
        case SK_LOGGER_LEVEL_DEBUG: return "DEBUG";
        case SK_LOGGER_LEVEL_INFO:  return "INFO";
        case SK_LOGGER_LEVEL_WARN:  return "WARN";
        case SK_LOGGER_LEVEL_ERROR: return "ERROR";
        case SK_LOGGER_LEVEL_FATAL: return "FATAL";
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
    return slash != NULL ? slash + 1 : path;
}

static void emit(sk_log_level_t level,
                 const char *source_file,
                 int source_line,
                 const char *format,
                 va_list args)
{
    char message[2048];

    if (level < sk_log_level) {
        return;
    }

    vsnprintf(message, sizeof(message), format, args);

    if (source_file != NULL && source_file[0] != '\0') {
        fprintf(stderr, "[%-5s] %s:%d: %s\n",
                level_name(level), basename_of(source_file), source_line, message);
    } else {
        fprintf(stderr, "[%-5s] %s\n", level_name(level), message);
    }
}

void sk_logger_message(sk_log_level_t level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    emit(level, NULL, 0, format, args);
    va_end(args);
}

void sk_logger_message_source(sk_log_level_t level,
                              const char *source_file,
                              int source_line,
                              const char *format,
                              ...)
{
    va_list args;
    va_start(args, format);
    emit(level, source_file, source_line, format, args);
    va_end(args);
}

void sk_logger_set_level(sk_log_level_t level)
{
    sk_log_level = level;
}

void sk_logger_init(void)
{
    /* nothing to set up for the stderr backend */
}

void sk_logger_deinit(void)
{
    /* no-op */
}
