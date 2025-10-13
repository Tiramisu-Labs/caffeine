#include <caffeine.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

ssize_t write_fully(int fd, const char *buf, size_t count) {
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t n = write(fd, buf + total_written, count - total_written);
        if (n < 0) {
            // Handle interrupt (EINTR) by retrying, or hard failure (EAGAIN/EWOULDBLOCK, etc.)
            if (errno == EINTR) continue;
            return -1; // Critical write failure
        }
        if (n == 0) {
            // Should not happen for write, but safety first
            return -1;
        }
        total_written += n;
    }
    return total_written;
}

void handle_request(int client_fd) {
    char header_buffer[4096];
    ssize_t bytes_read = 0;
    ssize_t total_bytes_read = 0;
    char *end_of_headers;
    
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

    const char *base_path = "/home/mcipolla/.config/caffeine/";
    char full_path[4096];
    char handler_name[256];

    char *path_start = strchr(header_buffer, '/');
    if (path_start) {
        char *path_end = strchr(path_start, ' ');
        if (path_end) {
            if (strstr(path_start, "..")) {
                const char* forbidden_response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
                write(client_fd, forbidden_response, strlen(forbidden_response));
                close(client_fd);
                return;
            }
            
            size_t path_len = path_end - path_start -1;
            if (path_len >= sizeof(handler_name)) {
                const char* too_long_response = "HTTP/1.1 414 URI Too Long\r\nContent-Length: 0\r\n\r\n";
                write(client_fd, too_long_response, strlen(too_long_response));
                close(client_fd);
                return;
            }
            
            strncpy(handler_name, path_start + 1, path_len);
            handler_name[path_len] = '\0';
            snprintf(full_path, sizeof(full_path), "%s%s", base_path, handler_name);
        } else {
            printf("bad request\n");
             const char* bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
             write(client_fd, bad_request, strlen(bad_request));
             close(client_fd);
             return;
        }
    } else {
        strncpy(handler_name, "handler.py", sizeof(handler_name));
        snprintf(full_path, sizeof(full_path), "%s%s", base_path, handler_name);
    }
    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        perror("pipe");
        close(client_fd);
        return;
    }

    pid_t handler_pid = fork();
    if (handler_pid < 0) {
        perror("fork");
        close(client_fd);
        return;
    }

    if (handler_pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);

        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null < 0) { 
            perror("open /dev/null");
            exit(EXIT_FAILURE); 
        }

        dup2(dev_null, STDERR_FILENO);
        close(dev_null);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        
        execlp(full_path, handler_name, NULL);
        perror("execlp");
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

    char response_buffer[4096];
    ssize_t response_bytes_read;
    ssize_t bytes_written;

    while ((response_bytes_read = read(stdout_pipe[0], response_buffer, sizeof(response_buffer))) > 0) {
        bytes_written = write_fully(client_fd, response_buffer, response_bytes_read);
        if (bytes_written < 0) break; 
    }

    close(stdout_pipe[0]);
    // if (shutdown(client_fd, SHUT_WR) < 0) {
    //     perror("shutdown SHUT_WR");
    // }
    waitpid(handler_pid, NULL, 0);
    close(client_fd);
}

void exec_worker() {    
    // create a new socket for this worker to connect back to the parent
    int ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_socket < 0) {
        perror("worker socket");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_un ipc_addr;
    memset(&ipc_addr, 0, sizeof(ipc_addr));
    ipc_addr.sun_family = AF_UNIX;
    strncpy(ipc_addr.sun_path, SOCKET_PATH, sizeof(ipc_addr.sun_path) - 1);

    // connect to the parent's listening socket
    while (connect(ipc_socket, (struct sockaddr *)&ipc_addr, sizeof(ipc_addr)) < 0) sleep(1);
    
    printf("Worker (PID %d) connected to parent.\n", getpid());

    char ready_signal = 'R';
    if (write(ipc_socket, &ready_signal, 1) < 0) {
        perror("initial ready signal write");
        close(ipc_socket);
        exit(EXIT_FAILURE);
    }
    
    while (1) {
        int client_fd = recv_fd(ipc_socket); 
        
        if (client_fd < 0) {
            printf("Worker exiting...\n");
            break;
        }
        printf("Worker (PID %d) received client FD %d. Handling request...\n", getpid(), client_fd);
        handle_request(client_fd);

        if (write(ipc_socket, &ready_signal, 1) < 0) {
            perror("ready signal write");
            break;
        }
    }

    close(ipc_socket);
    exit(EXIT_SUCCESS);
}