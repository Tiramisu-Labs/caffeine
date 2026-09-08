#include <shared_mem.h>

#include <sys/mman.h>
#include <stdio.h>

shm_layout_t *create_shared_map(void)
{
    shm_layout_t *map = mmap(
        NULL,
        sizeof(shm_layout_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (map == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    for (int i = 0; i < MAX_WORKERS; i++) {
        atomic_init(&map->workers[i].used, false);
        atomic_init(&map->workers[i].pid, 0);
        atomic_init(&map->workers[i].state, W_IDLE);
        atomic_init(&map->workers[i].start_ms, 0);
        atomic_init(&map->workers[i].deadline_ms, 0);
    }

    atomic_init(&map->active_workers, 0);

    return map;
}

void destroy_shared_map(shm_layout_t *map)
{
    if (map)
        munmap(map, sizeof(*map));
}