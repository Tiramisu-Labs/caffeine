#include <caffeine.h>
#include <caffeine_sig.h>
#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <deploy.h>
#include <log.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>

int send_fd(int socket, int fd_to_send) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char dummy_buffer = 'A';
    char control_buffer[CMSG_SPACE(sizeof(int))];

    iov[0].iov_base = &dummy_buffer;
    iov[0].iov_len = sizeof(dummy_buffer);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *(int *)CMSG_DATA(cmsg) = fd_to_send;

    if (sendmsg(socket, &msg, 0) < 0) {
        LOG_ERROR("sendmsg: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int recv_fd(int socket) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char dummy_buffer;
    char control_buffer[CMSG_SPACE(sizeof(int))];

    iov[0].iov_base = &dummy_buffer;
    iov[0].iov_len = sizeof(dummy_buffer);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);

    if (recvmsg(socket, &msg, 0) < 0) {
        if (errno == ECONNRESET) {
            LOG_DEBUG("recvmsg: Parent closed IPC socket (ECONNRESET). Signaling worker exit.");
            return -1; 
        }
        
        LOG_ERROR("recvmsg: %s", strerror(errno));
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        return *(int *)CMSG_DATA(cmsg);
    }
    return -1;
}

pid_t *g_worker_pids = NULL;
int main(int argc, char **argv) {
    init_config();

        
    if (parse_arguments(argc, argv) < 0) {
        free_and_exit(EXIT_FAILURE);
    }
    
    if (g_cfg.deploy) {
        int i = 0;
        while (g_cfg.deploy_start[i]) {
            if (is_flag( g_cfg.deploy_start[i])) break;
            handle_deploy( g_cfg.deploy_start[i]);
            i++;
        }
        free_and_exit(EXIT_SUCCESS);
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        free_and_exit(EXIT_FAILURE);
    }

    int enable = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        LOG_ERROR("setsockopt(SO_REUSEADDR): %s", strerror(errno));
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
        LOG_WARN("setsockopt(SO_REUSEPORT) failed. Connection dispatch may be less efficient: %s", strerror(errno));
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cfg.port);
    
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to bind to port %d: %s", g_cfg.port, strerror(errno));
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    if (listen(listen_fd, 4096) < 0) {
        LOG_ERROR("Couldn't listen on port %d: %s", g_cfg.port, strerror(errno));
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }

    if (g_cfg.daemonize) {
        LOG_INFO("Starting Caffeine server as a daemon...");
        daemonize();
    }
    LOG_INFO("Parent is now the process manager (PID %d).", getpid());
    LOG_INFO("Spawning %d worker processes...", g_cfg.workers);

    pid_t worker_pids[g_cfg.workers];
    for (int i = 0; i < g_cfg.workers; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            LOG_ERROR("fork failed during worker spawn: %s", strerror(errno));
            free_and_exit(EXIT_FAILURE);
        }
        
        if (pid == 0) {
            LOG_INFO("Worker process started (PID %d).", getpid());
            exec_worker(listen_fd);
            exit(EXIT_FAILURE); 
        } 
        worker_pids[i] = pid;
    }
    
    close(listen_fd); 
    LOG_DEBUG("Parent closed its copy of listen_fd.");
    LOG_INFO("Caffeine server running with %d workers on port %d.", g_cfg.workers, g_cfg.port);

    if (sig_init() < 0) {
        LOG_ERROR("Failed to initialize signals: %s", strerror(errno));
        for (int i = 0; i < g_cfg.workers; i++) kill(worker_pids[i], SIGTERM);
        free_and_exit(EXIT_FAILURE);
    }

    sigset_t oldmask, term_mask;
    if (sigemptyset(&term_mask) < 0 || sigaddset(&term_mask, SIGTERM) < 0) {
        LOG_ERROR("sigset operations failed.");
        for (int i = 0; i < g_cfg.workers; i++) kill(worker_pids[i], SIGTERM);
        free_and_exit(EXIT_FAILURE);
    }

    if (sigprocmask(SIG_BLOCK, &term_mask, &oldmask) < 0) {
        LOG_ERROR("sigprocmask BLOCK failed.");
        for (int i = 0; i < g_cfg.workers; i++) kill(worker_pids[i], SIGTERM);
        free_and_exit(EXIT_FAILURE);
    }

    LOG_INFO("Parent manager is now gracefully blocking, waiting for SIGTERM.");
    while (!g_shutdown_requested) sigsuspend(&oldmask);

    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    LOG_INFO("Parent performing cleanup and exiting. Killing workers.");
    for (int i = 0; i < g_cfg.workers; i++) kill(worker_pids[i], SIGTERM);
    free_and_exit(EXIT_SUCCESS);
    return 0;
}