#include <utils.h>

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>


char *trim_whitespace(char *str)
{
    if (!str)
        return NULL;

    while (isspace((unsigned char)*str))
        str++;

    if (*str == '\0')
        return str;

    char *end = str + strlen(str) - 1;

    while (end > str && isspace((unsigned char)*end))
        end--;

    end[1] = '\0';

    return str;
}


char *find_headers_end(
    const char *haystack,
    const char *needle,
    size_t size
)
{
    if (!haystack || !needle || size == 0)
        return NULL;

    size_t needle_len = strlen(needle);

    if (needle_len == 0 || needle_len > size)
        return NULL;

    for (size_t i = 0; i <= size - needle_len; i++) {
        if (memcmp(
                haystack + i,
                needle,
                needle_len
            ) == 0) {
            return (char *)(haystack + i);
        }
    }

    return NULL;
}


ssize_t write_fully(
    int fd,
    const char *buf,
    size_t count
)
{
    size_t total = 0;

    while (total < count) {

        ssize_t n = send(
            fd,
            buf + total,
            count - total,
            MSG_NOSIGNAL
        );

        if (n > 0) {
            total += (size_t)n;
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        return -1;
    }

    return (ssize_t)total;
}


uint64_t hash_path(const char *str)
{
    uint64_t hash = 5381;

    if (!str)
        return hash;

    while (*str) {
        hash = ((hash << 5) + hash) +
               (unsigned char)*str;
        str++;
    }

    return hash;
}


uint64_t now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000ULL +
           (uint64_t)ts.tv_nsec / 1000000ULL;
}