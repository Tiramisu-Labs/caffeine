#include <handler_registry.h>
#include <log.h>
#include <utils.h>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int ensure_capacity(handler_registry_t *registry)
{
    if (registry->count < registry->capacity)
        return 0;

    size_t new_capacity =
        registry->capacity == 0
            ? 16
            : registry->capacity * 2;

    handler_entry_t *entries = realloc(
        registry->entries,
        new_capacity * sizeof(*entries)
    );

    if (!entries)
        return -1;

    registry->entries = entries;
    registry->capacity = new_capacity;

    return 0;
}

static int is_shared_library(const char *name)
{
    size_t len;

    if (!name)
        return 0;

    len = strlen(name);

    return len > 3 &&
           strcmp(name + len - 3, ".so") == 0;
}

static int join_path(
    char *dst,
    size_t dst_size,
    const char *a,
    const char *b
)
{
    int written = snprintf(
        dst,
        dst_size,
        "%s/%s",
        a,
        b
    );

    if (written < 0 || (size_t)written >= dst_size)
        return -1;

    return 0;
}

static int add_handler(
    handler_registry_t *registry,
    const char *host,
    const char *route_path,
    const char *file_path
)
{
    if (ensure_capacity(registry) < 0)
        return -1;

    void *handle = dlopen(
        file_path,
        RTLD_NOW | RTLD_LOCAL
    );

    if (!handle) {
        LOG_ERROR(
            "failed to load handler %s: %s",
            file_path,
            dlerror()
        );

        return -1;
    }

    dlerror();

    handler_func func =
        (handler_func)dlsym(handle, "handler");

    const char *error = dlerror();

    if (error) {
        LOG_ERROR(
            "handler %s does not export 'handler': %s",
            file_path,
            error
        );

        dlclose(handle);
        return -1;
    }

    handler_entry_t *entry =
        &registry->entries[registry->count];

    memset(entry, 0, sizeof(*entry));

    entry->host = strdup(host);
    entry->path = strdup(route_path);

    if (!entry->host || !entry->path) {
        free(entry->host);
        free(entry->path);

        dlclose(handle);
        return -1;
    }

    entry->hash = hash_path(host);
    entry->hash ^= hash_path(route_path);

    entry->dl_handle = handle;
    entry->func = func;
    entry->timeout_ms = 0;

    registry->count++;

    LOG_INFO(
        "loaded handler %s%s",
        host,
        route_path
    );

    return 0;
}

static int build_route_path(
    char *dst,
    size_t dst_size,
    const char *prefix,
    const char *name
)
{
    size_t prefix_len = strlen(prefix);
    size_t name_len = strlen(name);

    if (strcmp(prefix, "/") == 0) {
        if (name_len + 2 > dst_size)
            return -1;

        dst[0] = '/';
        memcpy(dst + 1, name, name_len + 1);
        return 0;
    }

    if (prefix_len + name_len + 2 > dst_size)
        return -1;

    memcpy(dst, prefix, prefix_len);
    dst[prefix_len] = '/';
    memcpy(
        dst + prefix_len + 1,
        name,
        name_len + 1
    );

    return 0;
}

static int scan_route_directory(
    handler_registry_t *registry,
    const char *host,
    const char *directory,
    const char *route_prefix
)
{
    DIR *dir = opendir(directory);

    if (!dir) {
        LOG_ERROR(
            "failed to open handler directory %s: %s",
            directory,
            strerror(errno)
        );

        return -1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[PATH_MAX];

        if (join_path(
            full_path,
            sizeof(full_path),
            directory,
            entry->d_name
        ) < 0) {
            LOG_WARN(
                "handler path too long: %s/%s",
                directory,
                entry->d_name
            );
            continue;
        }

        struct stat st;

        if (stat(full_path, &st) < 0) {
            LOG_WARN(
                "failed to stat %s: %s",
                full_path,
                strerror(errno)
            );
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            char next_route[PATH_MAX];

            if (build_route_path(
                next_route,
                sizeof(next_route),
                route_prefix,
                entry->d_name
            ) < 0) {
                LOG_WARN(
                    "route path too long: %s/%s",
                    route_prefix,
                    entry->d_name
                );
                continue;
            }

            if (scan_route_directory(
                registry,
                host,
                full_path,
                next_route
            ) < 0) {
                closedir(dir);
                return -1;
            }

            continue;
        }

        if (!S_ISREG(st.st_mode))
            continue;

        if (!is_shared_library(entry->d_name))
            continue;

        size_t name_len = strlen(entry->d_name);

        char route_path[PATH_MAX];

        if (strcmp(entry->d_name, "index.so") == 0) {
            snprintf(
                route_path,
                sizeof(route_path),
                "%s",
                route_prefix
            );
        } else {
            char filename[PATH_MAX];

            if (name_len <= 3)
                continue;

            snprintf(
                filename,
                sizeof(filename),
                "%.*s",
                (int)(name_len - 3),
                entry->d_name
            );

            if (build_route_path(
                route_path,
                sizeof(route_path),
                route_prefix,
                filename
            ) < 0) {
                LOG_WARN(
                    "route path too long: %s/%s",
                    route_prefix,
                    filename
                );
                continue;
            }
        }

        if (add_handler(
            registry,
            host,
            route_path,
            full_path
        ) < 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

int handler_registry_init(handler_registry_t *registry)
{
    if (!registry)
        return -1;

    memset(registry, 0, sizeof(*registry));

    return 0;
}

int handler_registry_load_all(
    handler_registry_t *registry,
    const char *root
)
{
    if (!registry || !root)
        return -1;

    DIR *dir = opendir(root);

    if (!dir) {
        LOG_ERROR(
            "failed to open handler root %s: %s",
            root,
            strerror(errno)
        );

        return -1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char host_dir[PATH_MAX];

        if (join_path(
            host_dir,
            sizeof(host_dir),
            root,
            entry->d_name
        ) < 0) {
            LOG_WARN(
                "host path too long: %s/%s",
                root,
                entry->d_name
            );
            continue;
        }

        struct stat st;

        if (stat(host_dir, &st) < 0) {
            LOG_WARN(
                "failed to stat %s: %s",
                host_dir,
                strerror(errno)
            );
            continue;
        }

        if (!S_ISDIR(st.st_mode))
            continue;

        if (scan_route_directory(
            registry,
            entry->d_name,
            host_dir,
            "/"
        ) < 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);

    LOG_INFO(
        "loaded %zu handler(s)",
        registry->count
    );

    return 0;
}

handler_entry_t *handler_registry_find(
    const handler_registry_t *registry,
    const char *host,
    const char *path
)
{
    if (!registry || !host || !path)
        return NULL;

    uint64_t hash = hash_path(host);
    hash ^= hash_path(path);

    for (size_t i = 0; i < registry->count; i++) {
        handler_entry_t *entry =
            &registry->entries[i];

        if (entry->hash != hash)
            continue;

        if (strcmp(entry->host, host) != 0)
            continue;

        if (strcmp(entry->path, path) != 0)
            continue;

        return entry;
    }

    return NULL;
}

void handler_registry_destroy(
    handler_registry_t *registry
)
{
    if (!registry)
        return;

    for (size_t i = 0; i < registry->count; i++) {
        handler_entry_t *entry =
            &registry->entries[i];

        if (entry->dl_handle)
            dlclose(entry->dl_handle);

        free(entry->host);
        free(entry->path);
    }

    free(registry->entries);

    registry->entries = NULL;
    registry->count = 0;
    registry->capacity = 0;
}