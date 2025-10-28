#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <log.h>
#include <errno.h>

#define PID_FILE "/tmp/caffeine.pid"
#define LOG_FILE "/var/log/caffeine.log"

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
    return path;
}

void daemonize() {
    pid_t pid, sid;
    int fd;
    char pid_str[16];

    pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS); 

    sid = setsid();
    if (sid < 0) {
        LOG_ERROR("setsid: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        LOG_ERROR("chdir: %s", strerror(errno));
    }

    umask(0);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    char *log_path = get_log_path();
    fd = open(log_path, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        fprintf(stderr, "coudln't open file %s\n", LOG_FILE);
        exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (fd > 2) close(fd);
    
    free(log_path);

    fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LOG_ERROR("open pid file: ", strerror(errno));
        // Daemon should exit if it can't record its PID
        exit(EXIT_FAILURE); 
    }

    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    if (write(fd, pid_str, strlen(pid_str)) < 0) {
        LOG_ERROR("write pid file: ", strerror(errno));
        
        printf("removing file '%s'...\n", PID_FILE);
        if (remove(PID_FILE) == 0) printf("file '%s' removed\n", PID_FILE);
        else printf("error: unable to delete the file\n");    

        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);
}