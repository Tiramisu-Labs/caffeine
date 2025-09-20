#include "../include/caffeine.h"

// The send_fd function from before
int send_fd(int socket, int fd_to_send) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char dummy_buffer = 'A';
    char control_buffer[CMSG_SPACE(sizeof(int))];

    iov[0].iov_base = &dummy_buffer;
    iov[0].iov_len = sizeof(dummy_buffer);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    *(int *)CMSG_DATA(cmsg) = fd_to_send;

    if (sendmsg(socket, &msg, 0) < 0) {
        perror("sendmsg");
        return -1;
    }
    return 0;
}

// The recv_fd function from before
int recv_fd(int socket) {
    struct msghdr msg = {0};
    struct iovec iov[1];
    char dummy_buffer;
    char control_buffer[CMSG_SPACE(sizeof(int))];

    iov[0].iov_base = &dummy_buffer;
    iov[0].iov_len = sizeof(dummy_buffer);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);

    if (recvmsg(socket, &msg, 0) < 0) {
        perror("recvmsg");
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        return *(int *)CMSG_DATA(cmsg);
    }
    return -1;
}

int main(int argc, char **argv) {

    int workers = WORKER_COUNT;
    if (argc == 1) {
        fprintf(stderr, "Error: you must provide a path containing the executables\n");
    }
    if (argc > 1) {
        const char *offset = strchr(argv[1], '=');
        if (!offset) {
            fprintf(stderr, "Error: invalid option: %s\n", argv[1]);
            exit(EXIT_FAILURE);    
        }
        if (!strncmp(argv[1], "--workers=", offset - argv[1])) {
            int n_workers = atoi(&argv[1][(offset - argv[1]) + 1]);
            if (n_workers) workers = n_workers;
        }
    }
    // delete any tmp socket fd, if any
    unlink(SOCKET_PATH);
    
    int ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_socket < 0) {
        perror("ipc socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un ipc_addr;
    memset(&ipc_addr, 0, sizeof(ipc_addr));
    ipc_addr.sun_family = AF_UNIX;
    strncpy(ipc_addr.sun_path, SOCKET_PATH, sizeof(ipc_addr.sun_path) - 1);

    if (bind(ipc_socket, (struct sockaddr *)&ipc_addr, sizeof(ipc_addr)) < 0) {
        perror("ipc bind");
        exit(EXIT_FAILURE);
    }
    if (listen(ipc_socket, workers) < 0) {
        perror("ipc listen");
        exit(EXIT_FAILURE);
    }
    
    // Fork the worker processes
    for (int i = 0; i < workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) exec_worker();
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);
    
    bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(listen_fd, 10);
    
    printf("Parent listening for web requests on port 8080...\n");
    
    // Accept connections from workers
    int worker_fds[workers];
    for (int i = 0; i < workers; i++) {
        worker_fds[i] = accept(ipc_socket, NULL, NULL);
        if (worker_fds[i] < 0) {
            perror("ipc accept");
            exit(EXIT_FAILURE);
        }
    }
    
    int next_worker = 0;
    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        send_fd(worker_fds[next_worker], client_fd);
        printf("Sent client FD %d to worker %d.\n", client_fd, next_worker);
        close(client_fd);
        
        next_worker = (next_worker + 1) % workers;
    }

    close(listen_fd);
    close(ipc_socket);
    return 0;
}