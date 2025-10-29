#include <caffeine_sig.h>
#include <caffeine_utils.h>
#include <caffeine.h>
#include <log.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>

volatile sig_atomic_t g_shutdown_requested = 0;

void stop_server() {
    int fd = open(get_pid_path(), O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("PID file not found at %s. Is the server running? %s", 
                  get_pid_path(), strerror(errno));
        exit(EXIT_FAILURE);
    }

    char pid_str[16] = {0};
    ssize_t bytes_read = read(fd, pid_str, sizeof(pid_str) - 1);
    close(fd);

    if (bytes_read <= 0) {
        LOG_ERROR("Failed to read PID from file.");
        remove(get_pid_path());
        exit(EXIT_FAILURE);
    }
    
    pid_t parent_pid = (pid_t)atoi(pid_str);

    if (kill(parent_pid, SIGTERM) == 0) {
        LOG_INFO("Sent SIGTERM to Parent PID %d. Server should be shutting down.", parent_pid);
    } else {
        LOG_ERROR("Failed to send SIGTERM to PID %d: %s", parent_pid, strerror(errno));
        if (errno == ESRCH) {
            remove(get_pid_path());
            LOG_WARN("Stale PID file removed: %s", get_pid_path());
        }
        exit(EXIT_FAILURE);
    }
    remove(get_pid_path());
}

void sigterm_handler(int signum) {
    if (signum == SIGTERM) {
        g_shutdown_requested = 1;
        LOG_INFO("Parent received SIGTERM. Initiating graceful shutdown.");
    }
}

int sig_init()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        return -1;
    }
    return 0;
}