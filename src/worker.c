#include <caffeine.h>
#include <caffeine_utils.h>
#include <log.h>
#include <response.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

void handle_request(int client_fd) {
    char header_buffer[4096];
    ssize_t bytes_read = 0;
    ssize_t total_bytes_read = 0;
    char *end_of_headers;
    
    LOG_DEBUG("Handler (PID %d): Starting request for FD %d.", getpid(), client_fd);
    // find the headers
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
    char full_path[4096];
    char handler_name[256];

    char *path_start = strchr(header_buffer, '/');
    if (path_start) {
        char *path_end = strchr(path_start, ' ');
        if (path_end) {
            if (strstr(path_start, "..")) {
                write(client_fd, FORBIDDEN, strlen(FORBIDDEN));
                close(client_fd);
                return;
            }
            
            size_t path_len = path_end - path_start -1;
            if (path_len >= sizeof(handler_name)) {
                write(client_fd, TOO_LONG, strlen(TOO_LONG));
                close(client_fd);
                return;
            }
            
            strncpy(handler_name, path_start + 1, path_len);
            handler_name[path_len] = '\0';
            snprintf(full_path, sizeof(full_path), "%s%s", g_cfg.exec_path, handler_name);
        } else {
            write(client_fd, BAD_REQUEST, strlen(BAD_REQUEST));
            close(client_fd);
            return;
        }
    }

    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        LOG_ERROR("pipe: %s", strerror(errno));
        close(client_fd);
        return;
    }
    LOG_DEBUG("Handler (PID %d): Preparing to fork handler process for '%s'.", getpid(), full_path);
    pid_t handler_pid = fork();
    if (handler_pid < 0) {
        LOG_ERROR("fork: %s", strerror(errno));
        close(client_fd);
        return;
    }

    if (handler_pid == 0) {
        LOG_DEBUG("Grandchild (PID %d): Executing '%s'.", getpid(), full_path);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);

        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null < 0) { 
            LOG_ERROR("open /dev/null: %s", strerror(errno));
            exit(EXIT_FAILURE); 
        }

        dup2(dev_null, STDERR_FILENO);
        close(dev_null);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        
        execlp(full_path, handler_name, NULL);
        LOG_ERROR("execlp: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    // write the headers
    write(stdin_pipe[1], header_buffer, total_bytes_read);

    // stream the rest of the body
    int content_length = 0;
    char* cl_header = strstr(header_buffer, "Content-Length: ");
    if (cl_header) {
        content_length = atoi(cl_header + strlen("Content-Length: "));
    }
    
    ssize_t body_already_read = total_bytes_read - (end_of_headers - header_buffer) - 4;
    ssize_t body_bytes_streamed = body_already_read;

    while (body_bytes_streamed < content_length) {
        ssize_t chunk_size = read(client_fd, header_buffer, sizeof(header_buffer));
        if (chunk_size <= 0) break;
        write(stdin_pipe[1], header_buffer, chunk_size);
        body_bytes_streamed += chunk_size;
    }
    close(stdin_pipe[1]);

    char response_buffer[4096] = {0};
    ssize_t response_bytes_read;
    ssize_t bytes_written;
    LOG_DEBUG("Handler (PID %d): Starting response stream to client.", getpid());
    while ((response_bytes_read = read(stdout_pipe[0], response_buffer, sizeof(response_buffer))) > 0) {
        bytes_written = write_fully(client_fd, response_buffer, response_bytes_read);
        if (bytes_written < 0) break; 
    }
    
    if (strlen(response_buffer) == 0) {
        write(client_fd, NOT_FOUND, strlen(NOT_FOUND));
    }
    LOG_DEBUG("Handler (PID %d): Waiting for grandchild PID %d to exit.", getpid(), handler_pid);
    waitpid(handler_pid, NULL, 0);
    close(stdout_pipe[0]);
    close(client_fd);
    LOG_DEBUG("Handler (PID %d): Request complete and FD %d closed.", getpid(), client_fd);
}


void exec_worker() {    
    // create a new socket for this worker to connect back to the parent
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

    // connect to the parent's listening socket
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