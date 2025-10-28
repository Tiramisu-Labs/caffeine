#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <log.h>

void daemonize() {
    pid_t pid, sid;
    int fd;
    char pid_str[16];

    pid = fork();
    LOG_DEBUG("Child process (PID %d) continuing daemonization.", getpid());
    if (pid < 0) {
        LOG_ERROR("fork: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS); 

    sid = setsid();
    LOG_DEBUG("Process successfully started a new session (SID %d).", sid);
    if (sid < 0) {
        LOG_ERROR("setsid: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        LOG_ERROR("chdir: %s", strerror(errno));
    }
    LOG_DEBUG("Changed working directory to '/'.");
    umask(0);
    LOG_DEBUG("Closing standard file descriptors (0, 1, 2).");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    char *log_path = get_log_path();
    fd = open(log_path, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        LOG_ERROR("coudln't open file %s: ", LOG_FILE);
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
    LOG_DEBUG("Attempting to write PID %d to PID file '%s'.", getpid(), PID_FILE);
    if (write(fd, pid_str, strlen(pid_str)) < 0) {
        LOG_ERROR("write pid file: ", strerror(errno));
        
        LOG_INFO("removing file '%s'...", PID_FILE);
        if (remove(PID_FILE) == 0) LOG_INFO("file '%s' removed", PID_FILE);
        else LOG_ERROR("error: unable to delete the file");    

        close(fd);
        exit(EXIT_FAILURE);
    }

    close(fd);
}