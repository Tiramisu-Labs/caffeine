#ifndef WORKER_H
#define WORKER_H

#include <shared_mem.h>

void exec_worker(
    int listen_fd,
    shm_layout_t *map,
    int worker_index,
    const handler_registry_t *registry
);

#endif