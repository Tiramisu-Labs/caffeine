#include <request.h>
#include <response.h>
#include <log.h>
#include <utils.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>

// static void strupperncpy(char *__restrict __dest, const char *__restrict __src, size_t max_size) {
//     int i = 0;
//     while (__src[i] && i < max_size) {
//         __dest[i] = toupper(__src[i]);
//         i++;
//     }
//     __dest[i] = 0;
// }

bool read_headers(int client_fd, headers_t *hdrs) {
    ssize_t bytes_read = 0;
    bool headers_end_found = false;
    hdrs->body = NULL;

    while (hdrs->bytes_read < sizeof(hdrs->headers) - 1) {
        bytes_read = read(client_fd, hdrs->headers + hdrs->bytes_read, sizeof(hdrs->headers) - 1 - hdrs->bytes_read);

        if (bytes_read > 0) {
            hdrs->bytes_read += bytes_read;
            hdrs->headers[hdrs->bytes_read] = '\0';
            hdrs->headers_end = strstr(hdrs->headers, "\r\n\r\n");
            if (hdrs->headers_end) headers_end_found = true;
            // hdrs->headers_end = find_headers_end(hdrs->headers, "\r\n\r\n", hdrs->bytes_read);
            if (headers_end_found) {
                if (hdrs->headers_end == hdrs->headers) return false;
                int i = 0;
                int j = 0;
                while (hdrs->headers[i] && hdrs->headers[i] != ' ') {
                    hdrs->method[j++] = hdrs->headers[i++];
                    if (j == sizeof(hdrs->method)) return false;
                }
                if (strcmp(hdrs->method, "GET") && strcmp(hdrs->method, "HEAD") &&
                    strcmp(hdrs->method, "DELETE") && strcmp(hdrs->method, "PUT") &&
                    strcmp(hdrs->method, "POST") && strcmp(hdrs->method, "OPTIONS")) {
                        if(write(client_fd, BAD_REQUEST, BAD_REQUEST_LEN) < 0)
                            LOG_ERROR("failed to send BAD_REQUEST to client");
                        return false;
                }
                i++; // skip space
                if (hdrs->headers[i] != '/') {
                    if (write(client_fd, BAD_REQUEST, BAD_REQUEST_LEN) <0)
                        LOG_ERROR("failed to send BAD_REQUEST to client");
                    return false;
                }
                j = 0;
                i++; // skip '/'
                while (hdrs->headers[i] && hdrs->headers[i] != ' ' && hdrs->headers[i] != '?') {
                    hdrs->path[j] = hdrs->headers[i];
                    hdrs->handler_name[j] = hdrs->headers[i];
                    i++;
                    j++;
                    if (j == sizeof(hdrs->path) || j == sizeof(hdrs->handler_name)) return false;
                }
                hdrs->handler_name[j] = 0;
                if (hdrs->headers[i] == '?') {
                    hdrs->is_query = 1;
                    int k = 0;
                    while (hdrs->headers[i] && hdrs->headers[i] != ' ') {
                        hdrs->path[j++] = hdrs->headers[i++];
                        hdrs->query[k++] = hdrs->headers[i];
                        if (j == sizeof(hdrs->path)) return false;
                        if (k == sizeof(hdrs->query)) return false;
                    }
                    hdrs->query[k] = 0;
                }
                hdrs->path[j] = 0;
                if (j > 512) {
                    if (write(client_fd, TOO_LONG, TOO_LONG_LEN) <0)
                        LOG_ERROR("failed to send TOO_LONG to client");
                    return false;
                }
                break;
            }
        } else if (bytes_read == 0) {
            return false;
        } else if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = {.fd = client_fd, .events = POLLIN};

                int poll_result = poll(&pfd, 1, 5000);
                if (poll_result < 0) {
                    if (errno == EINTR) continue;
                    LOG_ERROR("poll failed: %s", strerror(errno));
                    return false;
                } else if (poll_result == 0) {
                    LOG_WARN("Client timeout while reading headers on FD %d.", client_fd);
                    return false;
                }
            } else {
                LOG_ERROR("read failed: %s", strerror(errno));                
                return false;
            }
        }
    }
    
    if (!hdrs->headers_end) return false;

    hdrs->host[0] = '\0';
    char *host_ptr = strstr(hdrs->headers, "\r\nHost:");
    if (!host_ptr) {
        host_ptr = strstr(hdrs->headers, "\r\nhost:");
    }
    if (host_ptr) {
        host_ptr += 7;
        while (*host_ptr == ' ' || *host_ptr == '\t') {
            host_ptr++;
        }
        int h = 0;
        int is_ipv6 = (*host_ptr == '[');
        while (host_ptr[h] && host_ptr[h] != '\r' && host_ptr[h] != '\n') {
            if (!is_ipv6 && host_ptr[h] == ':') break;
            if (is_ipv6 && host_ptr[h] == ']') {
                hdrs->host[h] = host_ptr[h];
                h++;
                break; 
            }
            hdrs->host[h] = host_ptr[h];
            h++;
            if (h == sizeof(hdrs->host) - 1) break;
        }
        hdrs->host[h] = '\0';
    }

    hdrs->content_length = 0;
    char *cl_ptr = strstr(hdrs->headers, "Content-Length:");
    if (!cl_ptr) {
        cl_ptr = strstr(hdrs->headers, "content-length:");
    }
    if (cl_ptr) {
        cl_ptr += 15;
        while (*cl_ptr == ' ' || *cl_ptr == '\t') cl_ptr++;
        hdrs->content_length = (size_t)strtoul(cl_ptr, NULL, 10);
    }

    hdrs->content_type[0] = '\0';
    char *ct_ptr = strstr(hdrs->headers, "Content-Type:");
    if (!ct_ptr) {
        ct_ptr = strstr(hdrs->headers, "content-type:");
    }
    if (ct_ptr) {
        ct_ptr += 13;
        while (*ct_ptr == ' ' || *ct_ptr == '\t') ct_ptr++;
        int c = 0;
        while (ct_ptr[c] && ct_ptr[c] != '\r' && ct_ptr[c] != '\n' && c < sizeof(hdrs->content_type) - 1) {
            hdrs->content_type[c] = ct_ptr[c];
            c++;
        }
        hdrs->content_type[c] = '\0';
    }

    return true;
}

bool read_body(int client_fd, headers_t *hdrs)
{
    hdrs->body = malloc(hdrs->content_length + 1);
    if (!hdrs->body) {
        LOG_ERROR("Out of memory allocating request body allocation track");
        if (write(client_fd, INTERNAL_ERROR, INTERNAL_ERROR_LEN) < 0) LOG_ERROR("Failed to write INTERNAL_ERROR");
        return false;
    }
    char *body_start = hdrs->headers_end + 4;
    size_t body_bytes_already_read = (hdrs->headers + hdrs->bytes_read) - body_start;
    if (body_bytes_already_read > 0) {
        size_t bytes_to_copy = (body_bytes_already_read > hdrs->content_length) ? hdrs->content_length : body_bytes_already_read;
        memcpy(hdrs->body, body_start, bytes_to_copy);
        hdrs->bytes_read = bytes_to_copy;
    } else {
        hdrs->bytes_read = 0;
    }
    while (hdrs->bytes_read < hdrs->content_length) {
        ssize_t r = read(client_fd, hdrs->body + hdrs->bytes_read, hdrs->content_length - hdrs->bytes_read);
        if (r > 0) {
            hdrs->bytes_read += r;
        } else if (r == 0) {
            break;
        } else if (r == -1) {
            if (errno == EINTR) continue;
            return false;
            break;
        }
    }
    hdrs->body[hdrs->content_length] = '\0';
    return true;
}