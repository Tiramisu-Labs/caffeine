#define _GNU_SOURCE         /* See feature_test_macros(7) */
#define _FILE_OFFSET_BITS 64
#include <caffeine.h>
#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <log.h>
#include <response.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h> 
#include <errno.h> 
#include <signal.h> 

ssize_t write_fully(int fd, const char *buf, size_t count) {
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t n = write(fd, buf + total_written, count - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;

        total_written += n;
    }
    return total_written;
}

static char* extract_token(const char *header, char terminator, char *buffer, size_t buf_size) {
    const char *end = strchr(header, terminator);
    if (!end || end - header >= buf_size) return NULL;
    strncpy(buffer, header, end - header);
    buffer[end - header] = '\0';
    return buffer;
}

static void setup_cgi_environment(char *header_buffer, char *end_of_headers, char *query_params, char *method, int *content_length)
{
    char content_type_val[256] = {0}; 

    char *current_line = strchr(header_buffer, '\n');
    if (current_line) current_line++;
        
    while (current_line && current_line < end_of_headers) {
        char *eol = strstr(current_line, "\r\n");
        if (!eol) break;

        *eol = '\0'; 
        char *separator = strchr(current_line, ':');

        if (separator) {
            *separator = '\0'; 
            
            char *key = trim_whitespace(current_line);
            char *value = trim_whitespace(separator + 1);

            if (strcasecmp(key, "Content-Length") == 0) {
                *content_length = atoi(value);
            } else if (strcasecmp(key, "Content-Type") == 0) {
                strncpy(content_type_val, value, sizeof(content_type_val) - 1);
                content_type_val[sizeof(content_type_val) - 1] = '\0';
            }
        }

        current_line = eol + 2;
    }
    
    char content_length_str[16];
    snprintf(content_length_str, sizeof(content_length_str), "%d", *content_length);
    
    setenv("REQUEST_METHOD", method, 1);
    setenv("CONTENT_LENGTH", content_length_str, 1);
    
    if (strlen(content_type_val) > 0) {
        setenv("CONTENT_TYPE", content_type_val, 1);
    }
    
    if (query_params) {
        int query_len = 0;
        while (query_params[query_len] != ' ') query_params++;
        if (query_len > 0) {
            char store_char = query_params[query_len];
            query_params[query_len] = 0;
            setenv("QUERY_STRING", query_params + 1, 1);
            query_params[query_len] = store_char;
        }
    }
}


void handle_request(int client_fd) {
    char header_buffer[8192];
    ssize_t bytes_read = 0;
    ssize_t total_bytes_read = 0;
    char *end_of_headers;
    int content_length = 0;
    
    LOG_DEBUG("Handler (PID %d): Starting request for FD %d.", getpid(), client_fd);
    
    while (total_bytes_read < sizeof(header_buffer) - 1) {
        bytes_read = read(client_fd, header_buffer + total_bytes_read, sizeof(header_buffer) - 1 - total_bytes_read);
        if (bytes_read <= 0) {
            close(client_fd);
            return;
        }
        total_bytes_read += bytes_read;
        header_buffer[total_bytes_read] = '\0';
        end_of_headers = strstr(header_buffer, "\r\n\r\n");
        if (end_of_headers) {
            break;
        }
    }
    
    if (!end_of_headers) {
        close(client_fd);
        return;
    }
    LOG_DEBUG("Handler (PID %d): Headers read and terminated.", getpid());
    
    char method[16] = {0};
    char handler_name[64] = {0};
    char full_handler[512] = {0};

    char *path_start = strchr(header_buffer, '/');
    if (!path_start || path_start == header_buffer + 1) {
        write(client_fd, BAD_REQUEST, strlen(BAD_REQUEST));
        close(client_fd);
        return;
    }
    
    if (!extract_token(header_buffer, ' ', method, sizeof(method))) {
        write(client_fd, BAD_REQUEST, strlen(BAD_REQUEST));
        close(client_fd);
        return;
    }
    
    char *path_end = strchr(path_start, ' ');
    if (path_end) {
        if (strstr(path_start, "..")) {
            write(client_fd, FORBIDDEN, strlen(FORBIDDEN));
            close(client_fd);
            return;
        }
        
        size_t path_len = path_end - path_start -1;
        if (path_len >= sizeof(full_handler)) {
            write(client_fd, TOO_LONG, strlen(TOO_LONG));
            close(client_fd);
            return;
        }
        
        strncpy(full_handler, path_start + 1, path_len);
        full_handler[path_len] = '\0';
    } else {
        write(client_fd, BAD_REQUEST, strlen(BAD_REQUEST));
        close(client_fd);
        return;
    }

    char *query_params = strchr(full_handler, '?');
    if (query_params) {
        char store = *query_params;
        *query_params = 0;
        strncpy(handler_name, full_handler, query_params - full_handler);
        *query_params = store;
    } else {
        strncpy(handler_name, full_handler, strlen(full_handler));
    }

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s%s", g_cfg.exec_path, handler_name);
    
    struct stat st;
    if (stat(full_path, &st) == -1) {
        if (errno == ENOENT) {
            LOG_WARN("File not found: %s", full_path);
            write(client_fd, NOT_FOUND, strlen(NOT_FOUND));
            close(client_fd);
            return;
        } else {
            LOG_ERROR("stat failed for %s: %s", full_path, strerror(errno));
            write(client_fd, INTERNAL_ERROR, strlen(INTERNAL_ERROR));
            close(client_fd);
            return;
        }
    }
    
    int stdin_pipe[2]; 
    if (pipe(stdin_pipe) < 0) {
        LOG_ERROR("pipe: %s", strerror(errno));
        close(client_fd);
        return;
    }

    LOG_DEBUG("Handler (PID %d): Preparing to fork handler process for '%s'.", getpid(), full_path);
    char *interpreter = NULL;
    char *handler_exec_path = full_path;

    size_t name_len = strlen(handler_name);
    
    if (name_len > 3) {
        if (strcmp(handler_name + name_len - 3, ".py") == 0) interpreter = "python3";
        else if (strcmp(handler_name + name_len - 3, ".js") == 0) interpreter = "node";
        else if (strcmp(handler_name + name_len - 3, ".sh") == 0) interpreter = "bash";
        else if (strcmp(handler_name + name_len - 3, ".pl") == 0) interpreter = "perl";
        else if (strcmp(handler_name + name_len - 3, ".rb") == 0) interpreter = "ruby";
        else if (name_len > 4 && strcmp(handler_name + name_len - 4, ".php") == 0) interpreter = "php";
    }
    if (interpreter) handler_exec_path = interpreter;

    pid_t handler_pid = fork();
    if (handler_pid < 0) {
        LOG_ERROR("fork: %s", strerror(errno));
        close(client_fd);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return;
    }

    if (handler_pid == 0) {
        LOG_DEBUG("Grandchild (PID %d): Executing '%s'.", getpid(), full_path);
        close(stdin_pipe[1]);

        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null < 0) { 
            LOG_ERROR("open /dev/null: %s", strerror(errno));
            exit(EXIT_FAILURE); 
        }

        dup2(dev_null, STDERR_FILENO);
        close(dev_null);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(client_fd, STDOUT_FILENO); 

        close(stdin_pipe[0]);
        close(client_fd);

        setup_cgi_environment(header_buffer, end_of_headers, query_params, method, &content_length);

        if (interpreter) {
            execlp(handler_exec_path, handler_exec_path, full_path, NULL);
        } else {
            // Binary execution: execlp(binary_path, handler_name, NULL)
            execlp(handler_exec_path, handler_name, NULL);
        }
        LOG_ERROR("execlp: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(stdin_pipe[0]);
    ssize_t body_already_read = total_bytes_read - (end_of_headers - header_buffer) - 4;
    ssize_t body_bytes_streamed = 0;

    if (body_already_read > 0) {
        char *body_start = end_of_headers + 4; 
        write(stdin_pipe[1], body_start, body_already_read);
        body_bytes_streamed += body_already_read;
    }
    
    // ssize_t remaining = (ssize_t)(content_length - body_bytes_streamed);

    // while (remaining > 0) {
    //     ssize_t chunk_size = splice(
    //         client_fd, NULL,
    //         stdin_pipe[1], NULL,
    //         (size_t)remaining, // Ensure count is positive
    //         SPLICE_F_MOVE | SPLICE_F_NONBLOCK | SPLICE_F_MORE
    //     );

    //     if (chunk_size > 0) {
    //         body_bytes_streamed += chunk_size;
    //         remaining -= chunk_size;
    //     } else if (chunk_size == 0) {
    //         usleep(100); 
    //     } else {
    //         if (errno == EINTR) continue;
    //         if (errno == EAGAIN || errno == EWOULDBLOCK) {
    //             usleep(100); 
    //             continue;
    //         }
    //         LOG_ERROR("splice failed: %s", strerror(errno));
    //         break; 
    //     }
    // }
    
    while (body_bytes_streamed < content_length) {
        ssize_t chunk_size = read(client_fd, header_buffer, sizeof(header_buffer));
        if (chunk_size <= 0) break;
        write(stdin_pipe[1], header_buffer, chunk_size);
    }
    
    close(stdin_pipe[1]);
    close(client_fd); 
    LOG_DEBUG("Handler (PID %d): FD %d handed off to child (PID %d). Returning to service loop.", getpid(), client_fd, handler_pid);
}

void exec_worker() {    
    signal(SIGCHLD, SIG_IGN); 

    int ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_socket < 0) {
        LOG_ERROR("worker socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_un ipc_addr;
    memset(&ipc_addr, 0, sizeof(ipc_addr));
    ipc_addr.sun_family = AF_UNIX;
    strncpy(ipc_addr.sun_path, get_socket_path(), sizeof(ipc_addr.sun_path) - 1);

    
    while (connect(ipc_socket, (struct sockaddr *)&ipc_addr, sizeof(ipc_addr)) < 0) sleep(1);
    
    LOG_INFO("Worker (PID %d) connected to parent.", getpid());

    char ready_signal = 'R';
    if (write(ipc_socket, &ready_signal, 1) < 0) {
        LOG_ERROR("initial ready signal write: %s", strerror(errno));
        close(ipc_socket);
        exit(EXIT_FAILURE);
    }
    
    while (1) {
        LOG_DEBUG("Worker (PID %d) waiting for client FD.", getpid());
        int client_fd = recv_fd(ipc_socket); 
        LOG_DEBUG("Worker (PID %d) successfully received FD %d.", getpid(), client_fd);
        if (client_fd < 0) {
            LOG_INFO("Worker exiting...");
            break;
        }
        LOG_INFO("Worker (PID %d) received client FD %d. Handling request...", getpid(), client_fd);

        handle_request(client_fd);
        
        LOG_DEBUG("Worker (PID %d) finished handling request for FD %d.", getpid(), client_fd);

        LOG_DEBUG("Worker (PID %d) signaling parent ready.", getpid());
        if (write(ipc_socket, &ready_signal, 1) < 0) {
            LOG_ERROR("ready signal write: %s", strerror(errno));
            break;
        }
    }

    close(ipc_socket);
    exit(EXIT_SUCCESS);
}
