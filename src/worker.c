#include "../include/caffeine.h"

void handle_request(int client_fd) {
    char header_buffer[4096];
    ssize_t bytes_read = 0;
    ssize_t total_bytes_read = 0;
    char *end_of_headers;
    
    // Read in a loop until we find the end of the headers
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
            break; // Found the end of the headers, exit the loop
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
        // No path found, use a default handler name. Still need to be build
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
        // Child process
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        
        // Use the full path for the executable and the handler name for argv[0]
        execlp(full_path, handler_name, NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }
    
    // Worker process
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    // Write the complete headers that have already been read
    write(stdin_pipe[1], header_buffer, total_bytes_read);

    // Stream the rest of the body
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
    while ((response_bytes_read = read(stdout_pipe[0], response_buffer, sizeof(response_buffer))) > 0) {
        write(client_fd, response_buffer, response_bytes_read);
    }

    waitpid(handler_pid, NULL, 0);
    close(stdout_pipe[0]);
    close(client_fd);
}

void exec_worker(void) {
    int ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_socket < 0) {
        perror("worker socket");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_un ipc_addr;
    memset(&ipc_addr, 0, sizeof(ipc_addr));
    ipc_addr.sun_family = AF_UNIX;
    strncpy(ipc_addr.sun_path, SOCKET_PATH, sizeof(ipc_addr.sun_path) - 1);

    while (connect(ipc_socket, (struct sockaddr *)&ipc_addr, sizeof(ipc_addr)) < 0) {
        sleep(1); // Wait for the parent to set up
    }
    
    printf("Worker (PID %d) connected to parent.\n", getpid());

    while (1) {
        int client_fd = recv_fd(ipc_socket);
        if (client_fd < 0) {
            printf("Worker exiting...\n");
            break;
        }
        printf("Worker (PID %d) received client FD %d.\n", getpid(), client_fd);
        handle_request(client_fd);
    }

    close(ipc_socket);
    exit(EXIT_SUCCESS);
}