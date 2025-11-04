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

#define MAX_ENV_STRINGS 128
#define MAX_ENV_LENGTH 512

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

static char* extract_token(const char *header, char terminator, char *buffer, size_t buf_size) {
    const char *end = strchr(header, terminator);
    if (!end || end - header >= buf_size) return NULL;
    strncpy(buffer, header, end - header);
    buffer[end - header] = '\0';
    return buffer;
}

void handle_grand_child(int client_fd, int *stdin_pipe, headers_t *hdrs)
{
    LOG_DEBUG("Handler (PID %d): Headers read and terminated.", getpid());

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s%s", g_cfg.exec_path, hdrs->handler_name);

    if (check_valid_path(client_fd, full_path) < 0) {
        exit(EXIT_FAILURE);
    }

    char *interpreter = NULL;
    char *handler_exec_path = full_path;
    size_t name_len = strlen(hdrs->handler_name);
    if (name_len > 3) {
        if (strcmp(hdrs->handler_name + name_len - 3, ".py") == 0) interpreter = "python3";
        else if (strcmp(hdrs->handler_name + name_len - 3, ".js") == 0) interpreter = "node";
        else if (strcmp(hdrs->handler_name + name_len - 3, ".sh") == 0) interpreter = "bash";
        else if (strcmp(hdrs->handler_name + name_len - 3, ".pl") == 0) interpreter = "perl";
        else if (strcmp(hdrs->handler_name + name_len - 3, ".rb") == 0) interpreter = "ruby";
        else if (name_len > 4 && strcmp(hdrs->handler_name + name_len - 4, ".php") == 0) interpreter = "php";
    }
    if (interpreter) handler_exec_path = interpreter;

    LOG_DEBUG("Grandchild (PID %d): Executing '%s'.", getpid(), hdrs->path);
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
    
    char env_buffer[MAX_ENV_STRINGS][MAX_ENV_LENGTH];
    char *envp[MAX_ENV_STRINGS + 1];
    setup_cgi_environment(hdrs, MAX_ENV_STRINGS, MAX_ENV_LENGTH, env_buffer, envp);

    if (interpreter) {
        char *const argv[] = {handler_exec_path, full_path, NULL};
        execvpe(handler_exec_path, argv, envp);
    }
    char *const argv[] = {handler_exec_path, hdrs->handler_name, NULL};
    execvpe(handler_exec_path, argv, envp);
}

void handle_request(int client_fd) {
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("FATAL: Worker failed to set fcntl(): %s", strerror(errno));
        return ;
    }
    if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("FATAL: Worker failed to set fcntl(): %s", strerror(errno));
        return ;
    }

    headers_t hdrs = {0};
    if (read_headers(client_fd, &hdrs) < 0) return;
    
    char *content_length = strcasestr(hdrs.headers, "Content-Length");
    if (content_length) {
        char length_buffer[32];
        if (!extract_token(content_length, ' ', length_buffer, sizeof(length_buffer))) {
            write(client_fd, BAD_REQUEST, strlen(BAD_REQUEST));
            return;
        }
        hdrs.content_length = atoi(length_buffer);
    }
    
    int stdin_pipe[2];
    if (pipe(stdin_pipe) < 0) {
        LOG_ERROR("pipe: %s", strerror(errno));
        return;
    }

    pid_t handler_pid = fork();
    if (handler_pid < 0) {
        LOG_ERROR("fork: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return;
    }

    if (handler_pid == 0) {
        handle_grand_child(client_fd, stdin_pipe, &hdrs);
        LOG_ERROR("execlp: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(stdin_pipe[0]);
    ssize_t body_already_read = hdrs.bytes_read - (hdrs.headers_end - hdrs.headers) - 4;
    ssize_t body_bytes_streamed = 0;

    if (body_already_read > 0) {
        char *body_start = hdrs.headers_end + 4;
        write(stdin_pipe[1], body_start, body_already_read);
        body_bytes_streamed += body_already_read;
    }
    
    ssize_t remaining = (ssize_t)(content_length - body_bytes_streamed);
    
    while (remaining > 0) {
        struct pollfd pfd = {.fd = client_fd, .events = POLLIN};
        int poll_result = poll(&pfd, 1, 5000);

        if (poll_result < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("poll failed during body stream: %s", strerror(errno));
            break;
        } else if (poll_result == 0) {
            LOG_WARN("Client timeout mid-stream on FD %d. Sent %zd/%d bytes.", client_fd, body_bytes_streamed, content_length);
            break;
        }

        char stream_buffer[4096];
        ssize_t chunk_size = read(client_fd, stream_buffer, sizeof(stream_buffer));

        if (chunk_size > 0) {
            if (write_fully(stdin_pipe[1], stream_buffer, chunk_size) < 0) {
                LOG_ERROR("write to pipe failed: %s", strerror(errno));
                break;
            }
            body_bytes_streamed += chunk_size;
            remaining -= chunk_size;
        } else if (chunk_size == 0) {
            break;
        } else if (chunk_size < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("read failed during body stream: %s", strerror(errno));
            break;
        }
    }
    
    close(stdin_pipe[1]);
    // close(client_fd);
    // waitpid(handler_pid, NULL, 0);
    LOG_DEBUG("Handler (PID %d): FD %d handed off to child (PID %d). Returning to service loop.", getpid(), client_fd, handler_pid);
}

void exec_worker(int listen_fd) {
    if (g_cfg.daemonize) worker_redirect_logs();

    signal(SIGCHLD, SIG_IGN);

    LOG_INFO("Worker (PID %d) is running and listening on shared FD %d.", getpid(), listen_fd);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;

    while (1) {
        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue; 
            }
            
            LOG_ERROR("Worker accept() failed: %s", strerror(errno));
            close(listen_fd);
            exit(EXIT_FAILURE);
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        LOG_INFO("Worker (PID %d) accepted connection from %s:%d on new FD %d.", 
                 getpid(), client_ip, ntohs(client_addr.sin_port), client_fd);

        handle_request(client_fd);
        
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
