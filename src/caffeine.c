#include <caffeine.h>
#include <config.h>
#include <monitor.h>
#include <signals.h>
#include <utils.h>
#include <handler_registry.h>
#include <log.h>
#include <shared_mem.h>

#include <poll.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <netinet/in.h>


static void handle_signals(
    int sigfd,
    shm_layout_t *map,
    const handler_registry_t *registry
)
{
    struct signalfd_siginfo si;

    while (read(sigfd, &si, sizeof(si)) == sizeof(si)) {
        switch (si.ssi_signo) {

        case SIGCHLD:
            reap_workers(map, registry);
            break;

        case SIGTERM:
        case SIGINT:
            g_shutdown_requested = 1;
            break;
        }
    }
}


int main(int argc, char **argv)
{
    int config_result = config_init(argc, argv);

    if (config_result < 0)
        return EXIT_FAILURE;

    if (config_result > 0)
        return EXIT_SUCCESS;


    /*
     * Worker state shared with the master.
     */
    shm_layout_t *map = create_shared_map();

    if (!map) {
        LOG_ERROR("failed to create shared worker map");
        return EXIT_FAILURE;
    }


    /*
     * Load every handler before spawning workers.
     *
     * Workers created with fork() inherit this registry
     * and the already loaded shared objects.
     */
    handler_registry_t registry;

    if (handler_registry_init(&registry) < 0) {
        LOG_ERROR("failed to initialize handler registry");
        destroy_shared_map(map);
        return EXIT_FAILURE;
    }

    if (handler_registry_load_all(&registry, g_cfg.handler_root) < 0) {
        LOG_ERROR("failed to load handlers from %s", g_cfg.handler_root);
        handler_registry_destroy(&registry);
        destroy_shared_map(map);
        return EXIT_FAILURE;
    }


    /*
     * Create listening socket.
     */
    g_cfg.listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (g_cfg.listen_fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        handler_registry_destroy(&registry);
        destroy_shared_map(map);
        return EXIT_FAILURE;
    }

    int enable = 1;

    if (setsockopt(
            g_cfg.listen_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &enable,
            sizeof(enable)
        ) < 0) {
        LOG_WARN("SO_REUSEADDR: %s", strerror(errno));
    }

    if (setsockopt(
            g_cfg.listen_fd,
            SOL_SOCKET,
            SO_REUSEPORT,
            &enable,
            sizeof(enable)
        ) < 0) {
        LOG_WARN("SO_REUSEPORT: %s", strerror(errno));
    }


    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cfg.port);

    if (bind(
            g_cfg.listen_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        ) < 0) {

        LOG_ERROR("bind: %s", strerror(errno));

        close(g_cfg.listen_fd);
        handler_registry_destroy(&registry);
        destroy_shared_map(map);

        return EXIT_FAILURE;
    }

    if (listen(g_cfg.listen_fd, 4096) < 0) {
        LOG_ERROR("listen: %s", strerror(errno));

        close(g_cfg.listen_fd);
        handler_registry_destroy(&registry);
        destroy_shared_map(map);

        return EXIT_FAILURE;
    }


    /*
     * Block signals before creating workers.
     *
     * This prevents a worker from exiting between fork()
     * and the installation of signalfd.
     */
    sigset_t mask;

    sigemptyset(&mask);

    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        LOG_ERROR("sigprocmask: %s", strerror(errno));

        close(g_cfg.listen_fd);
        handler_registry_destroy(&registry);
        destroy_shared_map(map);

        return EXIT_FAILURE;
    }


    int sigfd = signalfd(
        -1,
        &mask,
        SFD_NONBLOCK | SFD_CLOEXEC
    );

    if (sigfd < 0) {
        LOG_ERROR("signalfd: %s", strerror(errno));

        close(g_cfg.listen_fd);
        handler_registry_destroy(&registry);
        destroy_shared_map(map);

        return EXIT_FAILURE;
    }


    /*
     * Master monitor timer.
     */
    int tfd = timerfd_create(
        CLOCK_MONOTONIC,
        TFD_NONBLOCK | TFD_CLOEXEC
    );

    if (tfd < 0) {
        LOG_ERROR("timerfd_create: %s", strerror(errno));

        close(sigfd);
        close(g_cfg.listen_fd);

        handler_registry_destroy(&registry);
        destroy_shared_map(map);

        return EXIT_FAILURE;
    }


    struct itimerspec timer = {
        .it_interval = {
            .tv_sec = 5,
            .tv_nsec = 0
        },
        .it_value = {
            .tv_sec = 5,
            .tv_nsec = 0
        }
    };

    if (timerfd_settime(tfd, 0, &timer, NULL) < 0) {
        LOG_ERROR("timerfd_settime: %s", strerror(errno));

        close(tfd);
        close(sigfd);
        close(g_cfg.listen_fd);

        handler_registry_destroy(&registry);
        destroy_shared_map(map);

        return EXIT_FAILURE;
    }


    if (monitor_init() < 0) {
        LOG_ERROR("failed to initialize monitor");

        close(tfd);
        close(sigfd);
        close(g_cfg.listen_fd);

        handler_registry_destroy(&registry);
        destroy_shared_map(map);

        return EXIT_FAILURE;
    }


    /*
     * Spawn workers only after the whole runtime is ready.
     */
    if (g_cfg.min_workers > g_cfg.max_workers)
        g_cfg.min_workers = g_cfg.max_workers;

    for (int i = 0; i < g_cfg.min_workers; i++)
        spawn_worker(map, &registry);


    LOG_INFO(
        "server running with %d workers on port %d",
        g_cfg.current_workers,
        g_cfg.port
    );


    struct pollfd fds[2] = {
        {
            .fd = sigfd,
            .events = POLL_IN
        },
        {
            .fd = tfd,
            .events = POLL_IN
        }
    };


    /*
     * Master loop.
     *
     * The master does not execute handlers.
     * It only supervises workers and manages runtime state.
     */
    while (!g_shutdown_requested) {

        int ret = poll(fds, 2, -1);

        if (ret < 0) {
            if (errno == EINTR)
                continue;

            LOG_ERROR("poll: %s", strerror(errno));
            break;
        }

        if (fds[0].revents & POLL_IN)
            handle_signals(sigfd, map, &registry);

        if (fds[1].revents & POLL_IN)
            monitor_and_scale(tfd, map, &registry);
    }


    /*
     * Graceful shutdown.
     *
     * Do not compact worker slots. Every slot keeps its identity.
     */
    LOG_INFO("server shutting down...");

    for (int i = 0; i < MAX_WORKERS; i++) {
        if (!atomic_load(&map->workers[i].used))
            continue;

        pid_t pid = atomic_load(&map->workers[i].pid);

        if (pid > 0)
            kill(pid, SIGTERM);
    }


    while (g_cfg.current_workers > 0)
        reap_workers(map, &registry);


    monitor_cleanup();

    close(tfd);
    close(sigfd);
    close(g_cfg.listen_fd);

    /*
     * Workers are gone now, so it is safe for the master
     * to unload all remaining shared objects.
     */
    handler_registry_destroy(&registry);

    destroy_shared_map(map);

    return EXIT_SUCCESS;
}