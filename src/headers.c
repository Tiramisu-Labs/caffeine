#include <headers.h>
#include <response.h>
#include <log.h>
#include <caffeine_utils.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>

static void struppercpy(char *__restrict __dest, const char *__restrict __src) {
    int i = 0;
    while (__src[i]) {
        __dest[i] = toupper(__src[i]);
        i++;
    }
    __dest[i] = 0;
}

int setup_cgi_environment(headers_t *hdrs, int max_strings, int max_length, char env_buffer[max_strings][max_length], char **envp) {
    char *current_line = strchr(hdrs->headers, '\n');
    if (current_line) current_line++;

    int i = 0;
    while (current_line && current_line < hdrs->headers_end) {
        char *eol = strstr(current_line, "\r\n");
        if (!eol) break;
        *eol = '\0'; 
        char *separator = strchr(current_line, ':');
        if (separator) {
            *separator = '\0'; 
            char *key = trim_whitespace(current_line);
            char *value = trim_whitespace(separator + 1);
            if (strcasecmp(key, "Content-Length") == 0) {
                snprintf(env_buffer[i], max_length, "CONTENT_LENGTH=%s", value);
                envp[i] = env_buffer[i];
                i++;
            } else if (strcasecmp(key, "Content-Type") == 0) {
                snprintf(env_buffer[i], max_length, "CONTENT_TYPE=%s", value);
                envp[i] = env_buffer[i];
                i++;
            } else if (strcasecmp(key, "Authorization") == 0) {
                snprintf(env_buffer[i], max_length, "AUTH_TYPE=%s", value);
                envp[i] = env_buffer[i];
                i++;
                snprintf(env_buffer[i], max_length, "HTTP_AUTHORIZATION=%s", value);
                envp[i] = env_buffer[i];
                i++;
            } else {
                strncpy(env_buffer[i], "HTTP_", 5);
                struppercpy(env_buffer[i] + 5, key);
                snprintf(env_buffer[i] + (strlen(env_buffer[i])), max_length, "=%s", value);
                envp[i] = env_buffer[i];
                i++;
            }
        }
        current_line = eol + 2;
    }
    snprintf(env_buffer[i], max_length, "REQUEST_METHOD=%s", hdrs->method);
    envp[i] = env_buffer[i];
    i++;
    if (hdrs->is_query) {
        snprintf(env_buffer[i], max_length, "QUERY_STRING=%s", hdrs->query);
        envp[i] = env_buffer[i];
        i++;
    }
    envp[i] = NULL;
}

int check_valid_path(int client_fd, char *full_path) {
    struct stat st;
    if (stat(full_path, &st) == -1) {
        if (errno == ENOENT) {
            LOG_WARN("File not found: %s", full_path);
            write(client_fd, NOT_FOUND, NOT_FOUND_LEN);
            return -1;
        } else {
            LOG_ERROR("stat failed for %s: %s", full_path, strerror(errno));
            write(client_fd, INTERNAL_ERROR, INTERNAL_ERROR_LEN);
            return -1;
        }
    }
    return 0;
}

int read_headers(int client_fd, headers_t *hdrs) {
    ssize_t bytes_read = 0;

    int i = 0;
    while (hdrs->bytes_read < sizeof(hdrs->headers) - 1) {
        bytes_read = read(client_fd, hdrs->headers + hdrs->bytes_read, sizeof(hdrs->headers) - 1 - hdrs->bytes_read);

        if (bytes_read > 0) {
            hdrs->bytes_read += bytes_read;
            hdrs->headers[hdrs->bytes_read] = '\0';
            hdrs->headers_end = strstr(hdrs->headers, "\r\n\r\n");
            if (hdrs->headers_end) {
                int i = 0;
                int j = 0;
                while (hdrs->headers[i] && hdrs->headers[i] != ' ') {
                    hdrs->method[j++] = hdrs->headers[i++];
                }
                if (strcmp(hdrs->method, "GET") && strcmp(hdrs->method, "HEAD") &&
                    strcmp(hdrs->method, "DELETE") && strcmp(hdrs->method, "PUT") &&
                    strcmp(hdrs->method, "POST") && strcmp(hdrs->method, "OPTIONS")) {
                        write(client_fd, BAD_REQUEST, BAD_REQUEST_LEN);
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
                }
                hdrs->handler_name[j] = 0;
                if (hdrs->headers[i] == '?') {
                    hdrs->is_query = 1;
                    int k = 0;
                    while (hdrs->headers[i] && hdrs->headers[i] != ' ') {
                        hdrs->path[j++] = hdrs->headers[i++];
                        hdrs->query[k++] = hdrs->headers[i];
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