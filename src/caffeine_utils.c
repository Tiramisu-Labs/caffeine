#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <log.h>
#include <caffeine.h>
#include <sys/stat.h>
#include <pwd.h>
#include <ctype.h>

void free_and_exit(int status) {
    if (g_cfg.instance_name) free(g_cfg.instance_name);
    if (g_cfg.exec_path) free(g_cfg.exec_path);
    if (g_cfg.log_level) free(g_cfg.log_level);
    if (g_cfg.socket_path) free(g_cfg.socket_path);
    if (g_cfg.log_path) free(g_cfg.log_path);
    if (g_cfg.pid_path) free(g_cfg.pid_path);
    exit(status);
}

char* trim_whitespace(char *str) {
    if (!str) return NULL;
    char *end;

    while(isspace((unsigned char)*str)) str++;

    if(*str == 0) return str;

    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    *(end + 1) = 0;

    return str;
}

ssize_t write_fully(int fd, const char *buf, size_t count) {
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t n = write(fd, buf + total_written, count - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;

        total_written += n;
    }
    return total_written;
}

char *rstrstr(const char *__haystack, const char *__needle, ssize_t size)
{
    char *find = (char *)__haystack;
    int needle_len = strlen(__needle);
    int check = 0;

    if (size == 0) return NULL;
    for (int i = size - 1; i > 0; i--) {
        if (find[i] == __needle[needle_len - check]) {
            check++;
        }
        if (check == needle_len) return (find + i);
    }
    return NULL;
}

char* get_socket_path() {
    if (g_cfg.socket_path) return g_cfg.socket_path;

    if (!g_cfg.instance_name) {
        g_cfg.instance_name = strdup("caffeine");
    }
    size_t dir_len = strlen(SOCKET_PATH);
    size_t full_len = dir_len + strlen(g_cfg.instance_name) + strlen(SOCK_FILE_PREFIX) + strlen(SOCK_FILE_SUFFIX) + 1;
    
    char *path = malloc(full_len);
    if (path == NULL) return NULL;
    
    snprintf(path, dir_len + 1, "%s",  SOCKET_PATH);
    
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0700) != 0) {
            LOG_ERROR("mkdir log directory failed: %s", strerror(errno));
            free(path);
            return NULL;
        }
    }

    snprintf(path + dir_len, full_len - dir_len, "%s%s%s", SOCK_FILE_PREFIX, g_cfg.instance_name, SOCK_FILE_SUFFIX);
    LOG_DEBUG("Socket path resolved to: %s", path);
    g_cfg.socket_path = path;
    return g_cfg.socket_path;
}

char* get_pid_path() {
    if (g_cfg.pid_path) return g_cfg.pid_path;

    size_t dir_len = strlen(PID_PATH);
    size_t full_len = dir_len + strlen(g_cfg.instance_name) + strlen(PID_FILE_PREFIX) + strlen(PID_FILE_SUFFIX) + 1;
    
    char *path = malloc(full_len);
    if (path == NULL) return NULL;
    
    snprintf(path, dir_len + 1, "%s",  PID_PATH);
    
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0700) != 0) {
            LOG_ERROR("mkdir log directory failed: %s", strerror(errno));
            free(path);
            return NULL;
        }
    }

    snprintf(path + dir_len, full_len - dir_len, "%s%s%s", PID_FILE_PREFIX, g_cfg.instance_name, PID_FILE_SUFFIX);
    LOG_DEBUG("Pid path resolved to: %s", path);
    g_cfg.pid_path = path;
    return g_cfg.pid_path;
}

char* get_log_path() {
    if (g_cfg.log_path) return g_cfg.log_path;

    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) return NULL;
    
    const char *log_dir_suffix = LOG_PATH;
    const char *log_file_suffix = g_cfg.instance_name;

    size_t dir_len = strlen(pw->pw_dir) + strlen(log_dir_suffix);
    size_t full_len = dir_len + strlen(log_file_suffix) + 5;
    
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

    snprintf(path + dir_len, full_len - dir_len, "%s%s", log_file_suffix, ".log");
    LOG_DEBUG("Log path resolved to: %s", path);
    g_cfg.log_path = path;
    return g_cfg.log_path;
}

char* get_default_path() {
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) return NULL;
    
    size_t len = strlen(pw->pw_dir) + strlen(EXEC_PATH) + 1;
    char *path = malloc(len);
    if (path == NULL) return NULL;

    snprintf(path, len, "%s%s", pw->pw_dir, EXEC_PATH);
    return path;
}