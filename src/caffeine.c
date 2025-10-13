#include <caffeine.h>

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
    // to check, it's not working now as expected. Need to store the default location for executable, default is $HOME/.config/caffeine
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
    
    // Array to hold the connected worker FDs
    int worker_fds[workers];

    // Fork the worker processes
    for (int i = 0; i < workers; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
        if (pid == 0) {
            close(ipc_socket);
            exec_worker();
        } // Pass the listening socket to the worker
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);
    
    bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (listen(listen_fd, 10) < 0) {
        fprintf(stderr, "Couldn't listen on port 8080...\n");
    }
    printf("Parent listening for web requests on port 8080...\n");

    printf("Parent accepting connections from %d workers...\n", workers);
    for (int i = 0; i < workers; i++) {
        worker_fds[i] = accept(ipc_socket, NULL, NULL);
        if (worker_fds[i] < 0) { perror("ipc accept"); exit(EXIT_FAILURE); }
        printf("Worker %d connected on FD %d.\n", i, worker_fds[i]);
    }
    close(ipc_socket);

    struct pollfd pfds[workers];
    for (int i = 0; i < workers; i++) {
        pfds[i].fd = worker_fds[i];
        pfds[i].events = POLLIN;
    }
    
    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        if (poll(pfds, workers, -1) < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            close(client_fd);
            continue;
        }

        for (int i = 0; i < workers; i++) {
            if (pfds[i].revents & POLLIN) {
                char ready_byte;
                ssize_t bytes_read = read(pfds[i].fd, &ready_byte, 1); 
                
                if (bytes_read <= 0) {
                    fprintf(stderr, "Worker %d disconnected. Reaping.\n", i);
                    // TODO: respawn the worker here
                    break; 
                }

                send_fd(pfds[i].fd, client_fd);
                printf("Dispatched client FD %d to ready worker %d.\n", client_fd, i);
                close(client_fd);
                break;
            }
        }
    }

    close(listen_fd);
    for (int i = 0; i < workers; i++) close(worker_fds[i]);
    return 0;
}