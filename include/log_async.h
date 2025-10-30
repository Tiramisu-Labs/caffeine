#ifndef LOG_ASYNC_H
#define LOG_ASYNC_H

#include <pthread.h>
#include <stdarg.h>

#define LOG_MAX_MESSAGE_SIZE 1024

#define LOG_DEBUG(fmt, ...) log_async_enqueue(DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_async_enqueue(INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_async_enqueue(WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_async_enqueue(ERROR, fmt, ##__VA_ARGS__)

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

typedef struct log_message {
    char message[LOG_MAX_MESSAGE_SIZE];
    struct log_message *next;
} log_message_t;

typedef enum {
    DEBUG,
    INFO,
    WARN,
    ERROR
} log_level_t;

extern log_level_t g_log_level;

char* get_log_path();
void set_log_level(const char *level_str);
void init_logging(const char *log_file_path);
void shutdown_logging();
void log_async_enqueue(log_level_t level, const char *format, ...);

#endif