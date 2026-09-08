#ifndef MONITOR_H
#define CAFFEINE_MONITOR_H

#include <shared_mem.h>
#include <handler_registry.h>

void spawn_worker(
    shm_layout_t *map,
    const handler_registry_t *registry
);

void terminate_worker(
    shm_layout_t *map,
    int worker_index
);

void reap_workers(
    shm_layout_t *map,
    const handler_registry_t *registry
);

int monitor_init(void);
void monitor_cleanup(void);

void monitor_and_scale(
    int timer_fd,
    shm_layout_t *map,
    const handler_registry_t *registry
);

#endif