#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <arpa/inet.h>
#include <signal.h>
#include <shared_mem.h>

#define SOCKET_PATH "/tmp/"
#define SOCK_FILE_PREFIX "caffeine_"
#define SOCK_FILE_SUFFIX ".sock"
#define LOG_PATH "/.local/share/caffeine/"
#define EXEC_PATH "/.config/caffeine/"
#define PID_PATH "/tmp/"
#define CAFFEINE_FILE_PREFIX "caffeine_"
#define PID_FILE_SUFFIX ".pid"

typedef struct headers_s {
    char    method[16];
    char    path[512];
    char    query[512];
    char    protocol[16];
    char    headers[8192];
    char    handler_name[32];
    char    content_type[256];
    char    *headers_end;
    size_t  content_length;
    size_t  bytes_read;
    uint8_t is_query;
}   headers_t;

typedef const char* (*handler_func)(const char*, char*, size_t, size_t*);

typedef struct {
    char *path;
    unsigned long hash;
    void *dl_handle;
    handler_func func;
    time_t last_mtime;
    int timeout_ms;
} handler_entry_t;

typedef struct {
    handler_entry_t *entries;
    size_t size;
    size_t capacity;
} handler_cache_t;

typedef enum {
    W_IDLE = 'I',
    W_BUSY = 'B',
    W_EXIT = 'X',
    W_HEARTBEAT = 'H'
} worker_msg_t;

void exec_worker(int listen_fd, shm_layout_t* worker_map, int i);
void daemonize();

#endif