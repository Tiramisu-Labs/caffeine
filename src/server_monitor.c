#include <caffeine_monitor.h>
#include <caffeine_cfg.h>
#include <deploy.h>
#include <log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <caffeine_sig.h>
#include <caffeine_utils.h>

#define GRACE 200 // 1 second
#define HEARTBEAT_DEAD 1000 // 1 second

static proc_stats_t *last_stats = NULL;
static long last_total_jiffies = 0;

static int read_proc_stat(shm_worker_t *w, proc_stats_t *stat) {

    if (!w->used) return 0;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", w->pid);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char buffer[1024];
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    char comm[256], state;
    int res = sscanf(buffer,
        "%*d %255s %c "
        "%*d %*d %*d %*d %*d "
        "%*u %*u %*u %*u %*u "
        "%lu %lu "
        "%*d %*d %*d %*d %*d %*d %*d %lu",
        comm, &state,
        &stat->utime,
        &stat->stime,
        &stat->starttime
    );

    return (res == 5) ? 0 : -1;
}

static long read_total_jiffies(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return -1;

    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    long user, nice, system, idle, iowait, irq, softirq, steal;
    if (sscanf(line,
               "cpu  %ld %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &system, &idle,
               &iowait, &irq, &softirq, &steal) != 8)
        return -1;

    return user + nice + system + idle + iowait + irq + softirq + steal;
}

static void remove_worker_by_pid(pid_t pid, shm_layout_t* map) {
    for (int i = 0; i < g_cfg.current_workers; i++) {
        if (map->workers[i].pid == pid) {
            map->workers[i].used = 0;
            int last = g_cfg.current_workers - 1;
            if (i != last)
                map->workers[i].pid = map->workers[last].pid;
            g_cfg.current_workers--;
            map->worker_count--;
            return;
        }
    }

    LOG_WARN("caffeine: attempt to remove unknown worker %d", pid);
}

void spawn_worker(shm_layout_t* map) {
    if (g_cfg.current_workers >= g_cfg.max_workers)
        return;

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork failed: %s", strerror(errno));
        return;
    }

    if (pid == 0) {
        shm_worker_t* worker_ptr = NULL;
        for (int i = 0; i < g_cfg.max_workers; i++) {
            if (!map->workers[i].used) {
                map->workers[i].used = 1;
                map->workers[i].pid = pid;
                map->workers[i].state = W_IDLE;
                exec_worker(g_cfg.listen_fd, map, i);
                break;
            }
        }
        _exit(1);
    }
    g_cfg.current_workers++;
    map->worker_count++;
    LOG_INFO("worker spawned PID %d", pid);
}



void terminate_worker(pid_t pid) {
    if (pid <= 0)
        return;

    if (kill(pid, SIGTERM) < 0 && errno != ESRCH) {
        LOG_ERROR("caffeine: failed to SIGTERM %d: %s",
                  pid, strerror(errno));
    } else {
        LOG_INFO("caffeine: SIGTERM sent to worker %d", pid);
    }
}

void reap_workers(shm_layout_t* map) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            LOG_WARN("worker %d exited with code %d",
                     pid, WEXITSTATUS(status));
        }
        else if (WIFSIGNALED(status)) {
            LOG_WARN("worker %d killed by signal %d",
                     pid, WTERMSIG(status));
        } else {
            continue;
        }
        remove_worker_by_pid(pid, map);
        if (!g_shutdown_requested &&
            g_cfg.current_workers < g_cfg.min_workers) {
            spawn_worker(map);
        }
    }
}

void monitor_and_scale(int tfd, shm_layout_t* map) {
    uint64_t expirations;
    if (read(tfd, &expirations, sizeof(expirations)) < 0)
        return;

    int current_cnt = g_cfg.current_workers;
    if (current_cnt == 0)
        return;

    uint64_t now = now_ms();
    int busy_count = 0;
    int active_workers = 0;

    for (int i = 0; i < MAX_WORKERS; i++) {
        shm_worker_t *w = &map->workers[i];
        if (!w->used)
            continue;

        active_workers++;

        if (atomic_load(&w->state) == W_BUSY) {
            busy_count++;
            if (now - w->start_ms > map->handlers[w->handler_idx].timeout_ms) {
                LOG_ERROR("worker %d handler timeout", w->pid);
                kill(w->pid, SIGKILL);
                continue;
            }
        }
    }

    if (active_workers == 0) return;

    double occupancy_rate = (double)busy_count / active_workers;

    if (occupancy_rate > 0.80 && active_workers < g_cfg.max_workers) {
        spawn_worker(map);
    } 
    else if (occupancy_rate < 0.20 && active_workers > g_cfg.min_workers) {
        terminate_worker(map->workers[active_workers - 1].pid);
    }
}

int monitor_init(void) {
    last_stats = calloc(g_cfg.max_workers, sizeof(proc_stats_t));
    if (!last_stats)
        return -1;

    last_total_jiffies = 0;
    return 0;
}

void monitor_cleanup(void) {
    free(last_stats);
    last_stats = NULL;
}
