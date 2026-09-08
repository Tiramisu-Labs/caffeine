#include <headers.h>
#include <response.h>
#include <utils.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

static int parse_request_line(
    char *line,
    headers_t *hdrs
)
{
    char *method;
    char *target;
    char *protocol;

    method = strtok(line, " ");
    target = strtok(NULL, " ");
    protocol = strtok(NULL, " ");

    if (!method || !target || !protocol)
        return -1;

    if (strlen(method) >= sizeof(hdrs->method) ||
        strlen(protocol) >= sizeof(hdrs->protocol))
        return -1;

    if (target[0] != '/')
        return -1;

    if (strchr(target, '\0') == NULL)
        return -1;

    strcpy(hdrs->method, method);
    strcpy(hdrs->protocol, protocol);

    char *query = strchr(target, '?');

    if (query) {
        *query = '\0';
        query++;

        if (strlen(query) >= sizeof(hdrs->query))
            return -1;

        strcpy(hdrs->query, query);
        hdrs->is_query = 1;
    } else {
        hdrs->query[0] = '\0';
        hdrs->is_query = 0;
    }

    if (strlen(target) >= sizeof(hdrs->path))
        return -2;

    strcpy(hdrs->path, target);

    return 0;
}

static int parse_content_headers(
    char *line,
    headers_t *hdrs
)
{
    char *colon = strchr(line, ':');

    if (!colon)
        return 0;

    *colon = '\0';

    char *name = line;
    char *value = colon + 1;

    while (*value && isspace((unsigned char)*value))
        value++;

    if (strcasecmp(name, "Host") == 0) {
        if (strlen(value) >= sizeof(hdrs->host))
            return -1;

        strcpy(hdrs->host, value);
        return 0;
    }

    if (strcasecmp(name, "Content-Type") == 0) {
        if (strlen(value) >= sizeof(hdrs->content_type))
            return -1;

        strcpy(hdrs->content_type, value);
        return 0;
    }

    if (strcasecmp(name, "Content-Length") == 0) {
        char *end = NULL;

        errno = 0;

        unsigned long length = strtoul(value, &end, 10);

        if (errno != 0 || end == value || *end != '\0')
            return -1;

        hdrs->content_length = (size_t)length;
    }

    return 0;
}

int read_headers(int client_fd, headers_t *hdrs)
{
    if (!hdrs)
        return -1;

    memset(hdrs, 0, sizeof(*hdrs));

    size_t used = 0;

    while (used < sizeof(hdrs->headers) - 1) {
        ssize_t n = recv(
            client_fd,
            hdrs->headers + used,
            sizeof(hdrs->headers) - 1 - used,
            0
        );

        if (n > 0) {
            used += (size_t)n;
            hdrs->headers[used] = '\0';

            if (used >= 4 &&
                memcmp(
                    hdrs->headers + used - 4,
                    "\r\n\r\n",
                    4
                ) == 0) {
                break;
            }

            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        return -1;
    }

    if (used == sizeof(hdrs->headers) - 1 &&
        strstr(hdrs->headers, "\r\n\r\n") == NULL) {
        return -2;
    }

    char *headers_end = strstr(hdrs->headers, "\r\n\r\n");

    if (!headers_end)
        return -1;

    *headers_end = '\0';

    char *line = hdrs->headers;

    char *line_end = strstr(line, "\r\n");

    if (!line_end)
        return -1;

    *line_end = '\0';

    int request_result = parse_request_line(line, hdrs);

    if (request_result != 0)
        return request_result;

    line = line_end + 2;

    while (*line) {
        line_end = strstr(line, "\r\n");

        if (line_end)
            *line_end = '\0';

        if (parse_content_headers(line, hdrs) < 0)
            return -1;

        if (!line_end)
            break;

        line = line_end + 2;
    }

    if (hdrs->host[0] == '\0')
        return -1;

    if (strcasecmp(hdrs->method, "GET") != 0 &&
        strcasecmp(hdrs->method, "POST") != 0 &&
        strcasecmp(hdrs->method, "PUT") != 0 &&
        strcasecmp(hdrs->method, "DELETE") != 0 &&
        strcasecmp(hdrs->method, "PATCH") != 0 &&
        strcasecmp(hdrs->method, "HEAD") != 0) {
        return -3;
    }

    return 0;
}