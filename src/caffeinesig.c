#include <log.h>
#include <caffeinesig.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>

volatile sig_atomic_t shutdown_requested = 0;

void stop_server() {
    int fd = open(PID_FILE, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("PID file not found at %s. Is the server running? %s", 
                  PID_FILE, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char pid_str[16] = {0};
    ssize_t bytes_read = read(fd, pid_str, sizeof(pid_str) - 1);
    close(fd);

    if (bytes_read <= 0) {
        LOG_ERROR("Failed to read PID from file.");
        remove(PID_FILE);
        exit(EXIT_FAILURE);
    }
    
    pid_t parent_pid = (pid_t)atoi(pid_str);

    if (kill(parent_pid, SIGTERM) == 0) {
        LOG_INFO("Sent SIGTERM to Parent PID %d. Server should be shutting down.", parent_pid);
    } else {
        LOG_ERROR("Failed to send SIGTERM to PID %d: %s", parent_pid, strerror(errno));
        if (errno == ESRCH) {
            remove(PID_FILE);
            LOG_WARN("Stale PID file removed: %s", PID_FILE);
        }
        exit(EXIT_FAILURE);
    }
}

void sigterm_handler(int signum) {
    if (signum == SIGTERM) {
        shutdown_requested = 1;
        LOG_INFO("Parent received SIGTERM. Initiating graceful shutdown.");
    }
}