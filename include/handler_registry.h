#ifndef HANDLER_REGISTRY_H
#define HANDLER_REGISTRY_H

#include <stddef.h>
#include <stdint.h>
#include <caffeine.h>

typedef struct {
    char *host;
    char *path;

    uint64_t hash;

    void *dl_handle;
    handler_func func;

    uint32_t timeout_ms;
} handler_entry_t;

typedef struct {
    handler_entry_t *entries;
    size_t count;
    size_t capacity;
} handler_registry_t;

int handler_registry_init(handler_registry_t *registry);

int handler_registry_load_all(
    handler_registry_t *registry,
    const char *root
);

handler_entry_t *handler_registry_find(
    const handler_registry_t *registry,
    const char *host,
    const char *path
);

void handler_registry_destroy(
    handler_registry_t *registry
);

#endif