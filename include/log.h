#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

typedef enum {
    DEBUG,
    INFO,
    WARN,
    ERROR
} log_level_t;

extern log_level_t g_log_level;

#define LOG_DEBUG(fmt, ...) server_log(DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  server_log(INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  server_log(WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) server_log(ERROR, fmt, ##__VA_ARGS__)

void server_log(
    log_level_t level,
    const char *fmt,
    ...
);

void set_log_level(const char *level_str);

#endif