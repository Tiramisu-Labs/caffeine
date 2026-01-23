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
#include <time.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <netinet/in.h>
#include <errno.h>
#include <caffeine_monitor.h>
#include <sys/mman.h>

static void handle_signals(int sigfd, shm_layout_t* map) {
    struct signalfd_siginfo si;

    while (read(sigfd, &si, sizeof(si)) == sizeof(si)) {

        switch (si.ssi_signo) {

        case SIGCHLD:
            reap_workers(map);
            break;

        case SIGTERM:
        case SIGINT:
            g_shutdown_requested = 1;
            break;
        }
    }
}

int main(int argc, char **argv) {
    init_config();
    if (parse_arguments(argc, argv) < 0) free_and_exit(EXIT_FAILURE);

    shm_layout_t* map = create_shared_map();

    if (g_cfg.deploy) {
        int i = 0;
        while (g_cfg.deploy_start[i]) {
            if (is_flag(g_cfg.deploy_start[i])) break;
            handle_deploy(g_cfg.deploy_start[i]);
            i++;
        }
        free_and_exit(EXIT_SUCCESS);
    }

    g_cfg.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_cfg.listen_fd < 0) {
        fprintf(stderr, "%scaffeine: error: socket: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        free_and_exit(EXIT_FAILURE);
    }

    int enable = 1;
    setsockopt(g_cfg.listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    setsockopt(g_cfg.listen_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cfg.port);
    
    if (bind(g_cfg.listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "%scaffeine: error: bind failed: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        close(g_cfg.listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    if (listen(g_cfg.listen_fd, 4096) < 0) {
        fprintf(stderr, "%scaffeine: error: listen failed: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        close(g_cfg.listen_fd);
        free_and_exit(EXIT_FAILURE);
    }

    if (g_cfg.daemonize) daemonize();

    if (g_cfg.min_workers > g_cfg.max_workers) g_cfg.min_workers = g_cfg.max_workers;

    for (int i = 0; i < g_cfg.min_workers; i++) spawn_worker(map);
    
    fprintf(stdout, "%scaffeine: server running with %d workers on port %d%s\n\n", COLOR_GREEN, g_cfg.min_workers, g_cfg.port, COLOR_RESET);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    
    // new method: instad of definying custom sigaction for signals, push them into an fd with signalfd
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sigfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigfd < 0) {
        perror("signalfd");
        free_and_exit(EXIT_FAILURE);
    }
    
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) {
        perror("timerfd_create");
        free_and_exit(EXIT_FAILURE);
    }

    struct itimerspec its = {
        .it_interval = { .tv_sec = 5, .tv_nsec = 0 },
        .it_value    = { .tv_sec = 5, .tv_nsec = 0 }
    };

    timerfd_settime(tfd, 0, &its, NULL);

    struct pollfd fds[2];
    fds[0].fd = sigfd;
    fds[0].events = POLLIN;
    fds[1].fd = tfd;
    fds[1].events = POLLIN;

    if (monitor_init() < 0)
        free_and_exit(EXIT_FAILURE);

    while (!g_shutdown_requested) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN)
            handle_signals(sigfd, map);

        if (fds[1].revents & POLLIN)
            monitor_and_scale(tfd, map);
    }
    
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    LOG_INFO("Server shutting down...");
    for (int i = 0; i < g_cfg.current_workers; i++) {
        if (map->workers[i].used)
            kill(map->workers[i].pid, SIGTERM);
    }
    
    while (g_cfg.current_workers > 0)
        reap_workers(map);

    monitor_cleanup();
    munmap(map, sizeof(shm_layout_t));
    free_and_exit(EXIT_SUCCESS);
    return 0;
}