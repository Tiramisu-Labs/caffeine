#include "../include/caffeine.h"

void stream_request_to_handler(int client_fd, int handler_stdin) {
    char buffer[4096];
    ssize_t bytes_read;
    int content_length = -1;
    ssize_t body_bytes_read = 0;

    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        write(handler_stdin, buffer, bytes_read);
        char* content_length_header = strstr(buffer, "Content-Length: ");
        if (content_length_header) {
            content_length = atoi(content_length_header + strlen("Content-Length: "));
        }
        if (strstr(buffer, "\r\n\r\n")) {
            break;
        }
    }

    if (content_length > 0) {
        while (body_bytes_read < content_length) {
            bytes_read = read(client_fd, buffer, sizeof(buffer));
            if (bytes_read <= 0) break;
            write(handler_stdin, buffer, bytes_read);
            body_bytes_read += bytes_read;
        }
    }
    
    close(handler_stdin);
}

void handle_request(int client_fd) {
    int stdin_pipe[2], stdout_pipe[2];
    ssize_t bytes_read;
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
    // read first line to find path
    // char buffer[1024];
    // bytes_read = read(client_fd, buffer, sizeof(buffer));
    // printf("buffer: %s\n", buffer);
    // char path[1024];
    // int i, j = 0;
    // while (buffer[i] != '/') i++;
    // while (buffer[i] != ' ') {
    //     path[j++] = buffer[i++]; 
    // }
    // path[j] = 0;
    // printf("path %s\n", path);
    // char *prefix = "/home/mcipolla/.config/caffeine";
    // char full_path[2048];
    // sprintf(full_path, "%s%s%c", prefix, path, 0);
    // printf("full path %s\n", full_path);
    // while (path[i - 1] != '/') i--;
    // // extract only the executable name
    // char executable[128];
    // j = 0;
    // while (path[i] != ' ')  executable[j++] = path[i++];
    // executable[j] = 0;
    // printf("executable %s|\n", executable);
    if (handler_pid == 0) {
        close(stdin_pipe[1]);  // Close write end of stdin pipe
        close(stdout_pipe[0]); // Close read end of stdout pipe

        // Redirect child's stdin to the read end of stdin pipe
        dup2(stdin_pipe[0], STDIN_FILENO);
        // Redirect child's stdout to the write end of stdout pipe
        dup2(stdout_pipe[1], STDOUT_FILENO);

        // Close original pipe descriptors
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        execlp("/home/mcipolla/.config/caffeine/a.out", "a.out", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }

    close(stdin_pipe[0]);  // Close read end of stdin pipe
    close(stdout_pipe[1]); // Close write end of stdout pipe

    // write(stdin_pipe[1], buffer, bytes_read);
    stream_request_to_handler(client_fd, stdin_pipe[1]);

    char response_buffer[4096];
    while ((bytes_read = read(stdout_pipe[0], response_buffer, sizeof(response_buffer))) > 0) {
        write(client_fd, response_buffer, bytes_read);
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