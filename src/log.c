#include <log.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

log_level_t g_log_level = INFO;


static const char *log_level_to_string(log_level_t level)
{
    switch (level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default:    return "UNKNOWN";
    }
}


void set_log_level(const char *level_str)
{
    if (!level_str) {
        g_log_level = INFO;
        return;
    }

    char level[16];
    size_t len = strlen(level_str);

    if (len >= sizeof(level)) {
        g_log_level = INFO;
        return;
    }

    for (size_t i = 0; i < len; i++)
        level[i] = (char)toupper((unsigned char)level_str[i]);

    level[len] = '\0';

    if (strcmp(level, "DEBUG") == 0)
        g_log_level = DEBUG;
    else if (strcmp(level, "INFO") == 0)
        g_log_level = INFO;
    else if (strcmp(level, "WARN") == 0)
        g_log_level = WARN;
    else if (strcmp(level, "ERROR") == 0)
        g_log_level = ERROR;
    else {
        fprintf(
            stderr,
            "invalid log level '%s', using INFO\n",
            level_str
        );

        g_log_level = INFO;
    }
}


void server_log(
    log_level_t level,
    const char *fmt,
    ...
)
{
    if (level < g_log_level)
        return;

    time_t now = time(NULL);

    struct tm tm_now;

    if (localtime_r(&now, &tm_now) == NULL)
        return;

    char time_buf[20];

    strftime(
        time_buf,
        sizeof(time_buf),
        "%Y-%m-%d %H:%M:%S",
        &tm_now
    );

    fprintf(
        stderr,
        "%s [%s] [PID %d] ",
        time_buf,
        log_level_to_string(level),
        (int)getpid()
    );

    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}