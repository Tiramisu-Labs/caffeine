#include <shared_mem.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <caffeine_cfg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <caffeine_utils.h>
#include <dlfcn.h>
#include <log.h>

static void map_handler(shm_layout_t* map, char* path)
{
    DIR *dr;
    struct dirent *en;
    struct stat st; 
    char full_path[512];

    dr = opendir(path);

    if (dr == NULL) {
        printf("Could not open directory: %s\n", path);
        return;
    }

    while ((en = readdir(dr)) != NULL) {
        if (strcmp(en->d_name, ".") == 0 || strcmp(en->d_name, "..") == 0) {
            continue;
        }

        if (strcmp(&en->d_name[strlen(en->d_name) - 3], ".so") != 0) {
            continue;
        }
        snprintf(full_path, sizeof(full_path), "%s/%s", path, en->d_name);

        if (stat(full_path, &st) == -1) {
            perror("stat error");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            map_handler(map, full_path);
            printf("Directory: %s\n", en->d_name);
        } else {
            void *h = dlopen(full_path, RTLD_NOW | RTLD_LOCAL);
            if (!h) {
                LOG_ERROR("dlopen failed: %s", dlerror());
                return ;
            }
            int *t_ptr = (int *)dlsym(h, "timeout_val");
            unsigned long path_hash = hash_path(en->d_name);
            memset(map->handlers[map->handler_count].so_path, 0, 512);
            strncpy((char*)map->handlers[map->handler_count].so_path, full_path, 511);
            map->handlers[map->handler_count].hash = path_hash;
            map->handlers[map->handler_count].timeout_ms = t_ptr ? *t_ptr : 5000;
            map->handler_count++;
            dlclose(h);
        }
    }
    closedir(dr);
}

void* create_shared_map()
{
    shm_layout_t* map = mmap(NULL, sizeof(shm_layout_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    map->worker_count = g_cfg.min_workers;
    map_handler(map, g_cfg.exec_path);
    return map;
}