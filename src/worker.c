#define _GNU_SOURCE         /* See feature_test_macros(7) */
#define _FILE_OFFSET_BITS 64
#include <caffeine.h>
#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <log.h>
#include <response.h>
#include <headers.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h> 
#include <errno.h> 
#include <signal.h>
#include <limits.h> 
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <cJSON.h>

#define TIMEOUT -2

typedef struct monitor_s {
    char            *handler;
    unsigned int    timeout;
    int             status;
    struct timespec start;
    pthread_mutex_t mutex;
    int             client_fd;
}   monitor_t;

void* timeout_thread(void *args)
{
    monitor_t *monitor = (monitor_t *)args;
    int running = 0;
    for (;;) {
        pthread_mutex_lock(&(monitor->mutex));
        running = 1;
        while(running) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - monitor->start.tv_sec) * 1000 + (now.tv_nsec - monitor->start.tv_nsec) / 1000000;

            if (monitor->status == 1) {
                running = 0;
                break;
            } else if (elapsed_ms >= monitor->timeout && monitor->status == 0) {
                write(monitor->client_fd, REQUEST_TIMEOUT, REQUEST_TIMEOUT_LEN);
                LOG_WARN("killing worker %d, %s timeout", getpid(), monitor->handler);
                exit(TIMEOUT);
            }
            usleep(1000);
        }
        pthread_mutex_unlock(&(monitor->mutex));
    }
}

void worker_redirect_logs() {
    char *log_path = get_log_path();
    int log_fd;
    
    log_fd = open(log_path, O_RDWR | O_CREAT | O_APPEND, 0644);
    
    if (log_fd < 0) {
        LOG_ERROR("FATAL: Worker failed to open log file %s: %s", log_path, strerror(errno));
        return;
    }
    
    if (dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0) {
        close(log_fd);
        return;
    }

    if (log_fd > 2) close(log_fd);
    
    if (isatty(STDIN_FILENO)) freopen("/dev/null", "r", stdin); 
    if (isatty(STDOUT_FILENO)) freopen(log_path, "a", stdout);
    if (isatty(STDERR_FILENO)) freopen(log_path, "a", stderr); 
    
    LOG_INFO("Worker successfully forced redirection of STDOUT/STDERR to log file.");
}

int load_handler(handler_entry_t *entry, const char *so_path, struct stat *st, unsigned long path_hash) {
    if (entry->dl_handle) {
        dlclose(entry->dl_handle);
        if (entry->path) free(entry->path);
    }

    void *h = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        LOG_ERROR("dlopen failed: %s", dlerror());
        return -1;
    }

    handler_func f = (handler_func)dlsym(h, "handler");
    if (!f) {
        LOG_ERROR("Symbol 'handler' not found in %s", so_path);
        dlclose(h);
        return -1;
    }

    int *t_ptr = (int *)dlsym(h, "timeout_val");
    
    entry->dl_handle = h;
    entry->func = f;
    entry->path = strdup(so_path);
    entry->hash = path_hash;
    entry->last_mtime = st->st_mtime;
    entry->timeout_ms = t_ptr ? *t_ptr : 5000; 

    return 0;
}

handler_entry_t* get_handler_from_cache(handler_cache_t *cache, const char *handler_name, unsigned long path_hash) {
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s.so", g_cfg.exec_path, handler_name);

    struct stat st;
    if (stat(full_path, &st) < 0) {
        return NULL;
    }

    for (size_t i = 0; i < cache->size; i++) {
        if (cache->entries[i].hash == path_hash) {
            if (cache->entries[i].last_mtime != st.st_mtime) {
                if (load_handler(&cache->entries[i], full_path, &st, path_hash) != 0) return NULL;
            }
            return &cache->entries[i];
        }
    }

    if (cache->size >= cache->capacity) {
        cache->capacity = (cache->capacity == 0) ? 64 : cache->capacity * 2;
        cache->entries = realloc(cache->entries, sizeof(handler_entry_t) * cache->capacity);
    }

    handler_entry_t *new_entry = &cache->entries[cache->size];
    memset(new_entry, 0, sizeof(handler_entry_t));

    if (load_handler(new_entry, full_path, &st, path_hash) == 0) {
        cache->size++;
        return new_entry;
    }

    return NULL;
}

void handle_request(int client_fd, monitor_t *monitor, handler_cache_t *cache) {
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    headers_t hdrs = {0};
    if (read_headers(client_fd, &hdrs) < 0) return;

    cJSON *req_json = cJSON_CreateObject();
    cJSON_AddStringToObject(req_json, "handler", hdrs.handler_name);
    
    cJSON *req_headers = cJSON_CreateObject();
    cJSON_AddStringToObject(req_json, "raw_headers", hdrs.headers);
    
    char *json_request_str = cJSON_PrintUnformatted(req_json);

    unsigned long path_hash = hash_path(hdrs.handler_name);
    handler_entry_t *entry = get_handler_from_cache(cache, hdrs.handler_name, path_hash);

    if (!entry) {
        write(client_fd, NOT_FOUND, NOT_FOUND_LEN);
        cJSON_Delete(req_json);
        free(json_request_str);
        return;
    }

    pthread_mutex_lock(&(monitor->mutex));
    clock_gettime(CLOCK_MONOTONIC, &(monitor->start));
    monitor->handler = hdrs.handler_name;
    monitor->timeout = entry->timeout_ms;
    monitor->status = 0;
    monitor->client_fd = client_fd;
    pthread_mutex_unlock(&(monitor->mutex));

    char response_buffer[65536];
    size_t result_len = 0;
    
    const char *result_ptr = entry->func(
        json_request_str, 
        response_buffer, 
        sizeof(response_buffer), 
        &result_len
    );

    pthread_mutex_lock(&(monitor->mutex));
    monitor->status = 1;
    pthread_mutex_unlock(&(monitor->mutex));

    const char *final_json_ptr = (result_ptr != NULL) ? result_ptr : response_buffer;
    cJSON *res_json = cJSON_Parse(final_json_ptr);
    
    if (res_json) {
        cJSON *status = cJSON_GetObjectItem(res_json, "status");
        cJSON *body = cJSON_GetObjectItem(res_json, "body");
        
        int http_status = status ? status->valueint : 200;
        char *body_str = cJSON_IsObject(body) ? cJSON_PrintUnformatted(body) : strdup(body->valuestring);
        
        char http_resp[70000];
        int full_len = snprintf(http_resp, sizeof(http_resp),
            "HTTP/1.1 % d % s\r\n"
            "Content-Length: % zu\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n\r\n"
            "% s",
            http_status, (http_status == 200 ? "OK" : "Error"), 
            strlen(body_str), body_str);

        write(client_fd, http_resp, full_len);
        free(body_str);
        cJSON_Delete(res_json);
    } else {
        write(client_fd, INTERNAL_ERROR, INTERNAL_ERROR_LEN);
    }

    cJSON_Delete(req_json);
    free(json_request_str);
}

void exec_worker(int listen_fd) {
    if (g_cfg.daemonize) worker_redirect_logs();

    handler_cache_t cache = { .entries = NULL, .size = 0, .capacity = 0 };

    pthread_t thread;
    monitor_t timeout;
    if (pthread_mutex_init(&timeout.mutex, NULL) != 0) {
        // error
    }
    pthread_mutex_lock(&(timeout.mutex));
    if (pthread_create(&thread, NULL, timeout_thread, &timeout) != 0) {
        perror("pthread_create failed");
        exit(1);
    }
    pthread_detach(thread);
    // signal(SIGCHLD, SIG_IGN);

    LOG_INFO("Worker (PID %d) is running and listening on shared FD %d.", getpid(), listen_fd);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char client_ip[INET_ADDRSTRLEN];
    int client_fd;

    while (1) {
        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            
            LOG_ERROR("Worker accept() failed: %s", strerror(errno));
            close(listen_fd);
            exit(EXIT_FAILURE);
        }

        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        LOG_INFO("Worker (PID %d) accepted connection from %s:%d on new FD %d.",
                getpid(), client_ip, ntohs(client_addr.sin_port), client_fd);

        handle_request(client_fd, &timeout, &cache);
        
        if (close(client_fd) < 0) {
            if (errno == EBADF) {
                LOG_DEBUG("Expected EBADF (FD already closed by child) on client FD %d.", client_fd);
            } else {
                LOG_WARN("Unexpected error closing client FD %d: %s", client_fd, strerror(errno));
            }
        } else {
            LOG_DEBUG("Successfully closed client FD %d.", client_fd);
        }

        LOG_DEBUG("Worker (PID %d) finished and closed client FD %d. Waiting for next connection.", getpid(), client_fd);
    }

    LOG_INFO("Worker exiting...");
    close(listen_fd);
    exit(EXIT_SUCCESS);
}
