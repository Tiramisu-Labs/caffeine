#ifndef CAFFEINE_H
#define CAFFEINE_H

#include <stddef.h>
#include <stdint.h>

typedef struct headers_s {
    char method[16];
    char path[512];
    char query[512];
    char protocol[16];
    char host[256];

    char headers[8192];
    char content_type[256];

    size_t content_length;
    size_t bytes_read;

    uint8_t is_query;
} headers_t;

typedef const char *(*handler_func)(
    const char *request,
    char *response,
    size_t response_size,
    size_t *response_len
);

#endif