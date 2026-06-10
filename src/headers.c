#include <headers.h>
#include <response.h>
#include <log.h>
#include <caffeine_utils.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>

static void strupperncpy(char *__restrict __dest, const char *__restrict __src, size_t max_size) {
    int i = 0;
    while (__src[i] && i < max_size) {
        __dest[i] = toupper(__src[i]);
        i++;
    }
    __dest[i] = 0;
}

int read_headers(int client_fd, headers_t *hdrs) {
    ssize_t bytes_read = 0;

    int i = 0;
    while (hdrs->bytes_read < sizeof(hdrs->headers) - 1) {
        bytes_read = read(client_fd, hdrs->headers + hdrs->bytes_read, sizeof(hdrs->headers) - 1 - hdrs->bytes_read);

        if (bytes_read > 0) {
            hdrs->bytes_read += bytes_read;
            hdrs->headers[hdrs->bytes_read] = '\0';
            hdrs->headers_end = find_headers_end(hdrs->headers, "\r\n\r\n", hdrs->bytes_read);
            if (hdrs->headers_end) {
                if (hdrs->headers_end == hdrs->headers) return -1;
                int i = 0;
                int j = 0;
                while (hdrs->headers[i] && hdrs->headers[i] != ' ') {
                    hdrs->method[j++] = hdrs->headers[i++];
                    if (j == sizeof(hdrs->method)) return -1;
                }
                if (strcmp(hdrs->method, "GET") && strcmp(hdrs->method, "HEAD") &&
                    strcmp(hdrs->method, "DELETE") && strcmp(hdrs->method, "PUT") &&
                    strcmp(hdrs->method, "POST") && strcmp(hdrs->method, "OPTIONS")) {
                        write(client_fd, BAD_REQUEST, BAD_REQUEST_LEN);
                        return -1;
                }
                i++; // skip space
                if (hdrs->headers[i] != '/') {
                    write(client_fd, BAD_REQUEST, BAD_REQUEST_LEN);
                    return -1;
                }
                j = 0;
                i++; // skip '/'
                while (hdrs->headers[i] && hdrs->headers[i] != ' ' && hdrs->headers[i] != '?') {
                    hdrs->path[j] = hdrs->headers[i];
                    hdrs->handler_name[j] = hdrs->headers[i];
                    i++;
                    j++;
                    if (j == sizeof(hdrs->path) || j == sizeof(hdrs->handler_name)) return -1;
                }
                hdrs->handler_name[j] = 0;
                if (hdrs->headers[i] == '?') {
                    hdrs->is_query = 1;
                    int k = 0;
                    while (hdrs->headers[i] && hdrs->headers[i] != ' ') {
                        hdrs->path[j++] = hdrs->headers[i++];
                        hdrs->query[k++] = hdrs->headers[i];
                        if (j == sizeof(hdrs->path)) return -1;
                        if (k == sizeof(hdrs->query)) return -1;
                    }
                    hdrs->query[k] = 0;
                }
                hdrs->path[j] = 0;
                if (j > 512) {
                    write(client_fd, TOO_LONG, TOO_LONG_LEN);
                    return -1;
                }
                // add protocol eventually
                break;
            }
        } else if (bytes_read == 0) {
            return -1;
        } else if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = {.fd = client_fd, .events = POLLIN};

                int poll_result = poll(&pfd, 1, 5000);
                if (poll_result < 0) {
                    if (errno == EINTR) continue;
                    LOG_ERROR("poll failed: %s", strerror(errno));
                    return -1;
                } else if (poll_result == 0) {
                    LOG_WARN("Client timeout while reading headers on FD %d.", client_fd);
                    return -1;
                }
            } else {
                LOG_ERROR("read failed: %s", strerror(errno));                
                return -1;
            }
        }
    }
    
    if (!hdrs->headers_end) return -1;
    return 1;
}