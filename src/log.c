#include <log.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <pwd.h>

log_level_t log_level;

char* get_log_path() {
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) return NULL;
    
    const char *log_dir_suffix = "/.local/share/caffeine";
    const char *log_file_suffix = "/caffeine.log";

    size_t dir_len = strlen(pw->pw_dir) + strlen(log_dir_suffix);
    size_t full_len = dir_len + strlen(log_file_suffix) + 1;
    
    char *path = malloc(full_len);
    if (path == NULL) return NULL;
    
    snprintf(path, dir_len + 1, "%s%s", pw->pw_dir, log_dir_suffix);
    
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0700) != 0) {
            LOG_ERROR("mkdir log directory failed: %s", strerror(errno));
            free(path);
            return NULL;
        }
    }

    snprintf(path + dir_len, full_len - dir_len, "%s", log_file_suffix);
    LOG_DEBUG("Log path resolved to: %s", path);
    return path;
}

const char* log_level_to_str(log_level_t level) {
    switch (level) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void set_log_level(const char *level_str) {
    char upper_level[10];
    if (level_str == NULL || strlen(level_str) >= sizeof(upper_level)) {
        log_level = INFO;
        return;
    }
    
    for (int i = 0; level_str[i] != '\0'; i++) {
        upper_level[i] = toupper((unsigned char)level_str[i]);
    }
    upper_level[strlen(level_str)] = '\0';

    if (strcmp(upper_level, "DEBUG") == 0) {
        log_level = DEBUG;
    } else if (strcmp(upper_level, "INFO") == 0) {
        log_level = INFO;
    } else if (strcmp(upper_level, "WARN") == 0) {
        log_level = WARN;
    } else if (strcmp(upper_level, "ERROR") == 0) {
        log_level = ERROR;
    } else {
        fprintf(stderr, "Warning: Invalid log level '%s' specified. Defaulting to INFO.\n", level_str);
        log_level = INFO;
    }
}

void server_log(log_level_t level, const char *fmt, ...) {
    if (level < log_level) {
        return;
    }

    char time_buf[24];
    char log_message[1024] = {0};
    
    time_t timer;
    struct tm *tm_info;
    time(&timer);
    tm_info = localtime(&timer);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    va_list args;
    va_start(args, fmt);
    vsnprintf(log_message, sizeof(log_message), fmt, args);
    va_end(args);

    char final_output[1024 + 64];
    snprintf(final_output, sizeof(final_output), "%s [%s] [PID %d] %s\n",
             time_buf, log_level_to_str(level), getpid(), log_message);
    
    if (level >= WARN) {
        fprintf(stderr, "%s%s%s", level == ERROR ? COLOR_BRIGHT_RED : COLOR_BRIGHT_YELLOW, final_output, COLOR_RESET);
    } else {
        fprintf(stdout, "%s%s%s", level == DEBUG ? COLOR_CYAN : COLOR_GREEN, final_output, COLOR_RESET);
    }
}