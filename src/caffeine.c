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

pid_t *g_worker_pids = NULL;
int main(int argc, char **argv) {
    init_config();
        
    if (parse_arguments(argc, argv) < 0) free_and_exit(EXIT_FAILURE);
    
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
        fprintf(stderr, "%scaffeine: error: socket: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        free_and_exit(EXIT_FAILURE);
    }

    int enable = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        fprintf(stderr, "%scaffeine: error: setsockopt(SO_REUSEADDR): %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
        fprintf(stderr, "%scaffeine: error: setsockopt(SO_REUSEPORT) failed: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cfg.port);
    
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "%scaffeine: error: failed to bind to port %d: %s%s\n", COLOR_BRIGHT_RED, g_cfg.port, strerror(errno), COLOR_RESET);
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    if (listen(listen_fd, 4096) < 0) {
        fprintf(stderr, "%scaffeine: error: couldn't listen on port %d: %s%s\n", COLOR_BRIGHT_RED, g_cfg.port, strerror(errno), COLOR_RESET);
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }

    if (g_cfg.daemonize) {
        fprintf(stdout, "%scaffeine: starting Caffeine server as a daemon...%s\n", COLOR_GREEN, COLOR_RESET);
        daemonize();
    }
    fprintf(stdout, "caffeine: spawning %d worker processes...\n", g_cfg.workers);

    pid_t worker_pids[g_cfg.workers];
    for (int i = 0; i < g_cfg.workers; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            fprintf(stderr, "%scaffeine: error: fork: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
            free_and_exit(EXIT_FAILURE);
        }
        
        if (pid == 0) {
            fprintf(stdout, "caffeine: worker process started (PID %d)\n", getpid());
            exec_worker(listen_fd);
            exit(EXIT_FAILURE); 
        } 
        worker_pids[i] = pid;
    }
    
    close(listen_fd);
    
    fprintf(stdout, "%scaffeine: server running with %d workers on port %d%s\n\n", COLOR_GREEN, g_cfg.workers, g_cfg.port, COLOR_RESET);

    if (sig_init() < 0) {
        LOG_ERROR("Failed to initialize signals: %s. exiting...", strerror(errno));
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

    while (!g_shutdown_requested) sigsuspend(&oldmask);

    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    LOG_INFO("Parent performing cleanup and exiting. Killing workers.");
    for (int i = 0; i < g_cfg.workers; i++) kill(worker_pids[i], SIGTERM);
    free_and_exit(EXIT_SUCCESS);
    return 0;
}