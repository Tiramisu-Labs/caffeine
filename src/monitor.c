#include <monitor.h>
#include <config.h>
#include <signals.h>
#include <utils.h>
#include <log.h>
#include <worker.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


static int find_free_worker_slot(shm_layout_t *map)
{
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (!atomic_load(&map->workers[i].used))
            return i;
    }

    return -1;
}


static void mark_worker_dead(shm_layout_t *map, int worker_index)
{
    shm_worker_t *worker = &map->workers[worker_index];

    atomic_store(&worker->used, false);
    atomic_store(&worker->pid, 0);
    atomic_store(&worker->state, W_IDLE);
    atomic_store(&worker->start_ms, 0);
    atomic_store(&worker->deadline_ms, 0);

    atomic_fetch_sub(&map->active_workers, 1);
    g_cfg.current_workers--;
}


void spawn_worker(
    shm_layout_t *map,
    const handler_registry_t *registry
)
{
    if (g_cfg.current_workers >= g_cfg.max_workers)
        return;

    int worker_index = find_free_worker_slot(map);

    if (worker_index < 0) {
        LOG_ERROR("no free worker slot available");
        return;
    }

    shm_worker_t *worker = &map->workers[worker_index];

    /*
     * Reserve the slot before fork().
     *
     * The child must never race another child while looking
     * for an available slot.
     */
    atomic_store(&worker->used, true);
    atomic_store(&worker->state, W_IDLE);
    atomic_store(&worker->pid, 0);
    atomic_store(&worker->start_ms, 0);
    atomic_store(&worker->deadline_ms, 0);

    pid_t pid = fork();

    if (pid < 0) {
        LOG_ERROR("fork failed: %s", strerror(errno));

        atomic_store(&worker->used, false);

        return;
    }

    if (pid == 0) {
        /*
         * Child process.
         */
        atomic_store(&worker->pid, getpid());

        exec_worker(
            g_cfg.listen_fd,
            map,
            worker_index,
            registry
        );

        _exit(EXIT_FAILURE);
    }

    /*
     * Parent process.
     */
    atomic_store(&worker->pid, pid);

    atomic_fetch_add(&map->active_workers, 1);
    g_cfg.current_workers++;

    LOG_INFO(
        "worker spawned: index=%d pid=%d",
        worker_index,
        pid
    );
}


void terminate_worker(
    shm_layout_t *map,
    int worker_index
)
{
    if (worker_index < 0 || worker_index >= MAX_WORKERS)
        return;

    shm_worker_t *worker = &map->workers[worker_index];

    if (!atomic_load(&worker->used))
        return;

    pid_t pid = atomic_load(&worker->pid);

    if (pid <= 0)
        return;

    /*
     * Tell the worker to stop accepting new work.
     *
     * The worker will handle graceful draining.
     */
    if (atomic_load(&worker->state) != W_DRAINING)
        atomic_store(&worker->state, W_DRAINING);

    if (kill(pid, SIGTERM) < 0) {
        if (errno != ESRCH) {
            LOG_ERROR(
                "failed to terminate worker %d: %s",
                pid,
                strerror(errno)
            );
        }

        return;
    }

    LOG_INFO(
        "worker %d entering drain",
        pid
    );
}


void reap_workers(
    shm_layout_t *map,
    const handler_registry_t *registry
)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

        int worker_index = -1;

        for (int i = 0; i < MAX_WORKERS; i++) {
            if (atomic_load(&map->workers[i].used) &&
                atomic_load(&map->workers[i].pid) == pid) {
                worker_index = i;
                break;
            }
        }

        if (WIFEXITED(status)) {
            LOG_INFO(
                "worker %d exited with code %d",
                pid,
                WEXITSTATUS(status)
            );
        } else if (WIFSIGNALED(status)) {
            LOG_WARN(
                "worker %d killed by signal %d",
                pid,
                WTERMSIG(status)
            );
        }

        if (worker_index < 0) {
            LOG_WARN(
                "reaped unknown worker %d",
                pid
            );
            continue;
        }

        mark_worker_dead(map, worker_index);

        /*
         * Keep the configured minimum number of workers alive.
         */
        if (!g_shutdown_requested &&
            g_cfg.current_workers < g_cfg.min_workers) {

            spawn_worker(map, registry);
        }
    }
}


int monitor_init(void)
{
    return 0;
}


void monitor_cleanup(void)
{
}


void monitor_and_scale(
    int timer_fd,
    shm_layout_t *map,
    const handler_registry_t *registry
)
{
    uint64_t expirations;

    if (read(timer_fd, &expirations, sizeof(expirations)) < 0)
        return;

    (void)expirations;

    uint64_t now = now_ms();

    int active_workers = 0;
    int busy_workers = 0;

    for (int i = 0; i < MAX_WORKERS; i++) {

        shm_worker_t *worker = &map->workers[i];

        if (!atomic_load(&worker->used))
            continue;

        active_workers++;

        int state = atomic_load(&worker->state);

        if (state == W_BUSY) {
            busy_workers++;

            uint64_t deadline =
                atomic_load(&worker->deadline_ms);

            if (deadline != 0 && now >= deadline) {

                pid_t pid =
                    atomic_load(&worker->pid);

                LOG_ERROR(
                    "worker %d exceeded execution deadline",
                    pid
                );

                if (pid > 0)
                    kill(pid, SIGKILL);
            }
        }
    }

    if (active_workers == 0)
        return;

    /*
     * Very simple autoscaling for now.
     *
     * We can revisit this once worker execution and
     * connection handling are stable.
     */
    double occupancy =
        (double)busy_workers / active_workers;

    if (occupancy > 0.80 &&
        active_workers < g_cfg.max_workers) {

        spawn_worker(map, registry);
    }
}