#include "../include/caffeine.h"

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
        
        char buffer[1024];
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Worker (PID %d) received HTTP request on FD %d:\n%s\n", getpid(), client_fd, buffer);
            
            if (strstr(buffer, "GET /run-binary")) {
                printf("inside strstr\n");
                pid_t grandchild_pid = fork();
                if (grandchild_pid == 0) {
                    dup2(client_fd, STDOUT_FILENO);
                    execlp("./a.out", "./a.out", NULL);
                    perror("execlp");
                    exit(EXIT_FAILURE);
                }
                waitpid(grandchild_pid, NULL, 0);
            } else {
                const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, world!\n";
                write(client_fd, response, strlen(response));
            }
        }
        close(client_fd);
    }

    close(ipc_socket);
    exit(EXIT_SUCCESS); // Use exit to ensure the process terminates
}