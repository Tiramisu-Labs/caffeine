#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/types.h>

#define MAX_WORKERS 64

typedef enum {
    W_IDLE = 0,
    W_BUSY,
    W_DRAINING
} worker_state_t;

typedef struct {
    atomic_bool used;
    atomic_int pid;
    atomic_int state;

    atomic_uint_least64_t start_ms;
    atomic_uint_least64_t deadline_ms;
} shm_worker_t;

typedef struct {
    shm_worker_t workers[MAX_WORKERS];
    atomic_uint_least32_t active_workers;
} shm_layout_t;

shm_layout_t *create_shared_map(void);
void destroy_shared_map(shm_layout_t *map);

#endif