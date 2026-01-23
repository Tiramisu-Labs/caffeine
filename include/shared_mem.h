#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#define MAX_HANDLERS 128
#define MAX_WORKERS  64

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <stdatomic.h>

typedef struct {
    atomic_char           so_path[512];
    atomic_uint_least32_t timeout_ms;
    atomic_uint_least64_t hash;
    atomic_uint_least64_t version;
} shm_handler_t;

typedef struct {
    atomic_bool             used;
    atomic_int              pid;
    atomic_int              state;
    atomic_uint_least8_t    handler_idx;
    atomic_uint_least64_t   start_ms;
    atomic_uint_least64_t   last_heartbeat;
    atomic_uint_least64_t   handler_ver;
} shm_worker_t;

typedef struct shm_layout_s {
    uint32_t handler_count;
    shm_handler_t handlers[MAX_HANDLERS];

    uint32_t worker_count;
    shm_worker_t  workers[MAX_WORKERS];
}   shm_layout_t;

// Prototypes
void* create_shared_map();

#endif