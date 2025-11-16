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
#include <pthread.h>

void increase_worker_number() {
    pid_t pid = fork();
        
    if (pid < 0) {
        fprintf(stderr, "%scaffeine: error: fork: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
    }
    
    if (pid == 0) {
        fprintf(stdout, "caffeine: worker process started (PID %d)\n", getpid());
        g_cfg.workers_pid[g_cfg.current_workers] = pid;
        g_cfg.current_workers++;
        exec_worker(g_cfg.listen_fd);
        exit(EXIT_FAILURE); 
    } 
}

void decrese_worker_number() {
    g_cfg.current_workers--;
    kill(g_cfg.workers_pid[g_cfg.current_workers], SIGTERM);
}

void respawn_worker(pid_t pid) {
    for (int i = 0; i < g_cfg.max_workers; i++) {
        if (g_cfg.workers_pid[i] == pid) {
            pid_t worker_pid = fork();
            if (worker_pid == 0) {
                LOG_INFO("caffeine: worker process started (PID %d)\n", getpid());
                exec_worker(g_cfg.listen_fd);
                exit(EXIT_FAILURE); 
            }
            g_cfg.workers_pid[i] = worker_pid;
            break;
        }
    }
}

int read_proc_stat(pid_t pid, proc_stats_t *stat) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char buffer[1024];
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    char comm[256], state;
    int res = sscanf(buffer,
        "%*d %s %c %*d %*d %*d %*d %*d "
        "%*u %*u %*u %*u %*u "
        "%lu %lu %*d %*d %*d %*d %*d %*d %*d %lu",
        comm, &state, &stat->utime, &stat->stime, &stat->starttime);

    return (res == 5) ? 0 : -1;
}

long read_proc_mem(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[256];
    long rss_kb = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "VmRSS: %ld kB", &rss_kb) == 1) {
            break;
        }
    }
    fclose(fp);
    return rss_kb;
}

long read_total_jiffies() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;

    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    long user, nice, system, idle, iowait, irq, softirq, steal;
    if (sscanf(line, "cpu  %ld %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 8) {
        return -1;
    }

    return user + nice + system + idle + iowait + irq + softirq + steal;
}

void* scaler_thread(void *arg) {
    const int memory_threshold = 500*1024;

    for (;;) {
        for (int i = g_cfg.dead_workers_idx - 1; i >= 0; i--) {
            respawn_worker(g_cfg.dead_workers[i]);
            g_cfg.dead_workers_idx--;
        }

        long total_jiffies1 = read_total_jiffies();

        int current_workers = g_cfg.current_workers;
        int decrease_flag = 0;

        proc_stats_t stat1[current_workers];
        proc_stats_t stat2[current_workers];

    
        for (int i = 0; i < current_workers; i++) {
            read_proc_stat(g_cfg.workers_pid[i], &stat1[i]);
        }
        sleep(1);

        long total_jiffies2 = read_total_jiffies();

        for (int i = 0; i < current_workers; i++) {
            read_proc_stat(g_cfg.workers_pid[i], &stat2[i]);

            long delta_worker = (stat2[i].utime + stat2[i].stime) - (stat1[i].utime + stat1[i].stime);
            float cpu_percent = 100.0 * delta_worker / (float)(total_jiffies2 - total_jiffies1);

            long mem_kb = read_proc_mem(g_cfg.workers_pid[i]);

            if (cpu_percent > 80.0) {
                LOG_WARN("worker %d overloaded: %.1f%% CPU", g_cfg.workers_pid[i], cpu_percent);
                if (current_workers < g_cfg.max_workers) {
                    LOG_INFO("spawning new worker");
                    increase_worker_number();
                } else {
                    LOG_WARN("workers limits reached, no new worker will be spawned");
                }
            }

            if (mem_kb > memory_threshold) {
                LOG_WARN("worker %d memory threshold reached, restarting it", g_cfg.workers_pid[i]);
                respawn_worker(g_cfg.workers_pid[i]);
            }

            if (cpu_percent < 30.0 && g_cfg.current_workers > g_cfg.min_workers) {
                decrease_flag = 1;
            }
        }
        if (decrease_flag) decrese_worker_number();
        usleep(1000000);
    }
}

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

    g_cfg.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_cfg.listen_fd < 0) {
        fprintf(stderr, "%scaffeine: error: socket: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        free_and_exit(EXIT_FAILURE);
    }

    int enable = 1;
    if (setsockopt(g_cfg.listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        fprintf(stderr, "%scaffeine: error: setsockopt(SO_REUSEADDR): %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        close(g_cfg.listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    if (setsockopt(g_cfg.listen_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
        fprintf(stderr, "%scaffeine: error: setsockopt(SO_REUSEPORT) failed: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
        close(g_cfg.listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cfg.port);
    
    if (bind(g_cfg.listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "%scaffeine: error: failed to bind to port %d: %s%s\n", COLOR_BRIGHT_RED, g_cfg.port, strerror(errno), COLOR_RESET);
        close(g_cfg.listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    if (listen(g_cfg.listen_fd, 4096) < 0) {
        fprintf(stderr, "%scaffeine: error: couldn't listen on port %d: %s%s\n", COLOR_BRIGHT_RED, g_cfg.port, strerror(errno), COLOR_RESET);
        close(g_cfg.listen_fd);
        free_and_exit(EXIT_FAILURE);
    }

    if (g_cfg.daemonize) {
        fprintf(stdout, "%scaffeine: starting Caffeine server as a daemon...%s\n", COLOR_GREEN, COLOR_RESET);
        daemonize();
    }
    fprintf(stdout, "caffeine: spawning %d worker processes...\n", g_cfg.min_workers);

    if (g_cfg.min_workers > g_cfg.max_workers) {
        fprintf(stdout, "%swarning: workers number set too high: maximum for your configuration: %d\n", COLOR_YELLOW, g_cfg.max_workers);
        fprintf(stdout, "setting default workers accordingly%s\n", COLOR_RESET);
        g_cfg.min_workers = g_cfg.max_workers;
    }

    for (int i = 0; i < g_cfg.min_workers; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            fprintf(stderr, "%scaffeine: error: fork: %s%s\n", COLOR_BRIGHT_RED, strerror(errno), COLOR_RESET);
            free_and_exit(EXIT_FAILURE);
        }
        
        if (pid == 0) {
            fprintf(stdout, "caffeine: worker process started (PID %d)\n", getpid());
            exec_worker(g_cfg.listen_fd);
            exit(EXIT_FAILURE); 
        } 
        g_cfg.workers_pid[i] = pid;
    }
    
    // close(g_cfg.listen_fd);
    
    fprintf(stdout, "%scaffeine: server running with %d workers on port %d%s\n\n", COLOR_GREEN, g_cfg.min_workers, g_cfg.port, COLOR_RESET);

    if (sig_init() < 0) {
        LOG_ERROR("Failed to initialize signals: %s. exiting...", strerror(errno));
        for (int i = 0; i < g_cfg.min_workers; i++) kill(g_cfg.workers_pid[i], SIGTERM);
        free_and_exit(EXIT_FAILURE);
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, scaler_thread, NULL) != 0) {
        perror("pthread_create failed");
        exit(1);
    }
    pthread_detach(thread);

    sigset_t oldmask, term_mask;
    if (sigemptyset(&term_mask) < 0 || sigaddset(&term_mask, SIGTERM) < 0 || sigaddset(&term_mask, SIGCHLD) < 0) {
        LOG_ERROR("sigset operations failed.");
        for (int i = 0; i < g_cfg.min_workers; i++) kill(g_cfg.workers_pid[i], SIGTERM);
        free_and_exit(EXIT_FAILURE);
    }

    if (sigprocmask(SIG_BLOCK, &term_mask, &oldmask) < 0) {
        LOG_ERROR("sigprocmask BLOCK failed.");
        for (int i = 0; i < g_cfg.min_workers; i++) kill(g_cfg.workers_pid[i], SIGTERM);
        free_and_exit(EXIT_FAILURE);
    }

    while (!g_shutdown_requested) sigsuspend(&oldmask);

    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    LOG_INFO("Parent performing cleanup and exiting. Killing workers.");
    for (int i = 0; i < g_cfg.max_workers; i++) kill(g_cfg.workers_pid[i], SIGTERM);
    free_and_exit(EXIT_SUCCESS);
    return 0;
}