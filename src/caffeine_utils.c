#include <caffeine_utils.h>
#include <log.h>
#include <caffeine.h>
#include <sys/stat.h>
#include <pwd.h>

config_t g_cfg; // global g_cfg file so it is inherited by workers processes

void init_config()
{
    memset(&g_cfg, 0, sizeof(config_t));
    g_cfg.port = DEFAULT_PORT;
    g_cfg.workers = DEFAULT_WORKERS;
    g_cfg.log_level = DEFAULT_LOG_LEVEL;
    g_cfg.daemonize = 0;
    g_cfg.instance_name = NULL;
    g_cfg.exec_path = NULL;
    g_cfg.log_level = NULL;
    g_cfg.socket_path = NULL;
    g_cfg.log_path = NULL;
    g_cfg.pid_path = NULL;
}

void free_and_exit(int status) {
    if (g_cfg.instance_name) free(g_cfg.instance_name);
    if (g_cfg.exec_path) free(g_cfg.exec_path);
    if (g_cfg.log_level) free(g_cfg.log_level);
    if (g_cfg.socket_path) free(g_cfg.socket_path);
    if (g_cfg.log_path) free(g_cfg.log_path);
    if (g_cfg.pid_path) free(g_cfg.pid_path);
    exit(status);
}

char* get_socket_path() {
    if (g_cfg.socket_path) return g_cfg.socket_path;

    if (!g_cfg.instance_name) {
        g_cfg.instance_name = strdup("caffeine");
    }
    size_t dir_len = strlen(SOCKET_PATH);
    size_t full_len = dir_len + strlen(g_cfg.instance_name) + 6;
    
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

    snprintf(path + dir_len, full_len - dir_len, "%s%s", g_cfg.instance_name, ".sock");
    LOG_DEBUG("Socket path resolved to: %s", path);
    g_cfg.socket_path = path;
    return g_cfg.socket_path;
}

char* get_pid_path() {
    if (g_cfg.pid_path) return g_cfg.pid_path;

    size_t dir_len = strlen(PID_FILE);
    size_t full_len = dir_len + strlen(g_cfg.instance_name) + 5;
    
    char *path = malloc(full_len);
    if (path == NULL) return NULL;
    
    snprintf(path, dir_len + 1, "%s",  PID_FILE);
    
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0700) != 0) {
            LOG_ERROR("mkdir log directory failed: %s", strerror(errno));
            free(path);
            return NULL;
        }
    }

    snprintf(path + dir_len, full_len - dir_len, "%s%s", g_cfg.instance_name, ".pid");
    LOG_DEBUG("Pid path resolved to: %s", path);
    g_cfg.pid_path = path;
    return g_cfg.pid_path;
}

char* get_log_path() {
    if (g_cfg.log_path) return g_cfg.log_path;

    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) return NULL;
    
    const char *log_dir_suffix = "/.local/share/caffeine";
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