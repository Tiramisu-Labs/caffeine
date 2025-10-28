#ifndef LOG_H
#define LOG_H

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdarg.h>
#include <caffeine.h>

#define LOG_DEBUG(fmt, ...) server_log(DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) server_log(INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) server_log(WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) server_log(ERROR, fmt, ##__VA_ARGS__)

#define COLOR_RED           "\x1b[31m"
#define COLOR_GREEN         "\x1b[32m"
#define COLOR_YELLOW        "\x1b[33m"
#define COLOR_BLUE          "\x1b[34m"
#define COLOR_MAGENTA       "\x1b[35m"
#define COLOR_CYAN          "\x1b[36m"
#define COLOR_WHITE         "\x1b[37m"
#define COLOR_GRAY          "\x1b[90m"
#define COLOR_BRIGHT_RED    "\x1b[91m"
#define COLOR_BRIGHT_YELLOW "\x1b[93m"
#define COLOR_RESET         "\x1b[0m"

typedef enum {
    DEBUG,
    INFO,
    WARN,
    ERROR
} log_level_t;

extern log_level_t log_level;

char* get_log_path();
void server_log(log_level_t level, const char *fmt, ...);
void set_log_level(const char *level_str);

#endif