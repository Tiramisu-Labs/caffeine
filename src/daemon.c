#include <caffeine_utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <log.h>

void daemonize() {
    pid_t pid, sid;
    int fd;
    char pid_str[16];

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "%scaffeine: error: fork: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS); 

    sid = setsid();
    fprintf(stdout, "caffeine: process successfully started a new session (SID %d)\n", sid);
    if (sid < 0) {
        fprintf(stderr, "%scaffeine: error: setsid: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        free_and_exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        fprintf(stderr, "%scaffeine: error: chdir: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        free_and_exit(EXIT_FAILURE);
    }
    
    umask(0);
    int stderr_cpy = dup(STDERR_FILENO);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    char *log_path = get_log_path();
    fd = open(log_path, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        dprintf(stderr_cpy, "%scaffeine: error: coudln't open file %s: %s\n", COLOR_BRIGHT_RED, log_path, COLOR_RESET);
        exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        dprintf(stderr_cpy, "%scaffeine: error: dup2: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        close(fd);
        exit(EXIT_FAILURE);
    }

    fflush(stdout);
    fflush(stderr);

    if (fd > 2) close(fd);

    char *pid_file = get_pid_path();
    fd = open(pid_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        dprintf(stderr_cpy, "%scaffeine: error: open pid file: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        exit(EXIT_FAILURE); 
    }

    dprintf(stderr_cpy, "%scaffeine: debug: attempting to write PID %d to PID file '%s'%s\n", COLOR_BLUE, getpid(), pid_file, COLOR_RESET);
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    if (write(fd, pid_str, strlen(pid_str)) < 0) {
        dprintf(stderr_cpy, "%scaffeine: error: write pid file: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        
        dprintf(stderr_cpy, "%scaffeine: removing file '%s'...%s\n", COLOR_BRIGHT_YELLOW, pid_file, COLOR_RESET);
        if (remove(pid_file) == 0) dprintf(stderr_cpy, "caffeine: file '%s' removed\n", pid_file);
        else dprintf(stderr_cpy, "%scaffeine: error: unable to delete the file%s\n", COLOR_BRIGHT_RED, COLOR_RESET);

        close(fd);
        exit(EXIT_FAILURE);
    }
    close(stderr_cpy);
    close(fd);
}