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
#include <limits.h> 
#include <dlfcn.h>

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
        const char *end = strchr(content_length, ' ');
        if (!end || end - content_length >= sizeof(length_buffer)) {
            write(client_fd, BAD_REQUEST, strlen(BAD_REQUEST));
            return;
        }
        strncpy(length_buffer, content_length, end - content_length);
        length_buffer[end - content_length] = '\0';
        hdrs.content_length = strtol(length_buffer, NULL, 10);
        if (hdrs.content_length == LONG_MIN || hdrs.content_length == LONG_MAX) {
            LOG_ERROR("error: %s", strerror(errno));
        }
    }
    
    if (hdrs.content_length > 0) {
        ssize_t remaining = (ssize_t)(content_length - ( hdrs.bytes_read - (hdrs.headers_end - hdrs.headers) - 4));
        while (remaining > 0) {
            struct pollfd pfd = {.fd = client_fd, .events = POLLIN};
            int poll_result = poll(&pfd, 1, 5000);

            if (poll_result < 0) {
                if (errno == EINTR) continue;
                LOG_ERROR("poll failed during body stream: %s", strerror(errno));
                break;
            } else if (poll_result == 0) {
                LOG_WARN("something went wrong while reading the body");
                break;
            }
            
            ssize_t chunk_size = read(client_fd, hdrs.headers + hdrs.bytes_read, sizeof(hdrs.headers) - hdrs.bytes_read);

            if (chunk_size > 0) {
                hdrs.bytes_read += chunk_size;
                remaining -= chunk_size;
                hdrs.headers[hdrs.bytes_read] = 0;
            } else if (chunk_size == 0) {
                break;
            } else if (chunk_size < 0) {
                if (errno == EINTR) continue;
                LOG_ERROR("read failed during body stream: %s", strerror(errno));
                break;
            }
        } 
    }

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s%s.so", g_cfg.exec_path, hdrs.handler_name);
    void *handle = dlopen(full_path, RTLD_NOW | RTLD_LOCAL);
    handler_func handler = (handler_func)dlsym(handle, "handler");
    if (!handler) {
        LOG_ERROR("error: dlsym: %s\n", dlerror());
        dlclose(handle);
        return ;
    }

    char *result = handler(hdrs.headers);

    char header[256];
    int body_len = strlen(result);
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        body_len);

    write(client_fd, header, header_len);
    write(client_fd, result, body_len);

    dlclose(handle);
}

void exec_worker(int listen_fd) {
    if (g_cfg.daemonize) worker_redirect_logs();

    // signal(SIGCHLD, SIG_IGN);

    LOG_INFO("Worker (PID %d) is running and listening on shared FD %d.", getpid(), listen_fd);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char client_ip[INET_ADDRSTRLEN];
    int client_fd;

    while (1) {
        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            
            LOG_ERROR("Worker accept() failed: %s", strerror(errno));
            close(listen_fd);
            exit(EXIT_FAILURE);
        }

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
