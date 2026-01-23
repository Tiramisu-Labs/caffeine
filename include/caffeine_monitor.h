#ifndef CAFFEINE_MONITOR_H
#define CAFFEINE_MONITOR_H

#include <sys/types.h>
#include <pthread.h>
#include <shared_mem.h>

typedef struct {
    unsigned long utime;
    unsigned long stime;
    unsigned long starttime;
} proc_stats_t;


void monitor_and_scale(int timerfd, shm_layout_t* map);
void reap_workers(shm_layout_t* map);
void spawn_worker(shm_layout_t* map);
void terminate_worker(pid_t pid);
int monitor_init(void);
void monitor_cleanup(void);

#endif