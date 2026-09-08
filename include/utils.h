#ifndef UTILS_H
#define CAFFEINE_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

char *trim_whitespace(char *str);

ssize_t write_fully(
    int fd,
    const char *buf,
    size_t count
);

uint64_t hash_path(const char *str);

uint64_t now_ms(void);

#endif