#include <caffeine.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>

void display_help(const char *progname) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Caffeine: A high-performance pre-fork web server.\n\n");
    printf("Options:\n");
    printf("  -h, --help            Display this help message and exit.\n");
    printf("  -d, --daemon          Run the server as a background daemon.\n");
    printf("  --path=<path>         Set the base path for executable handlers (Default: ~/.config/caffeine/).\n");
    printf("  --port=<port>         Set the listening port (Default: %d).\n", DEFAULT_PORT);
    printf("  --workers=<num>       Set the number of worker processes (Default: %d).\n", DEFAULT_WORKERS);
    printf("  --log-level=<level>   Set logging verbosity (DEBUG, INFO, WARN, ERROR) (Default: %s).\n", DEFAULT_LOG_LEVEL);
    exit(EXIT_SUCCESS);
}

void display_log_file() {
    char *log_path = get_log_path();
    int fd = open(log_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open log file at %s: %s\n", log_path, strerror(errno));
        return;
    }

    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        if (write(STDOUT_FILENO, buffer, bytes_read) < 0) {
            perror("Error writing to stdout");
            break;
        }
    }
    free(log_path);
    close(fd);
}

char* get_default_path() {
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) return NULL;
    
    size_t len = strlen(pw->pw_dir) + strlen("/.config/caffeine/") + 1;
    char *path = malloc(len);
    if (path == NULL) return NULL;

    snprintf(path, len, "%s/.config/caffeine/", pw->pw_dir);
    return path;
}

void parse_arguments(int argc, char **argv, config_t *cfg) {
    cfg->port = DEFAULT_PORT;
    cfg->workers = DEFAULT_WORKERS;
    cfg->log_level = DEFAULT_LOG_LEVEL;
    cfg->daemonize = 0;
    cfg->exec_path = NULL;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            if (cfg->exec_path) free(cfg->exec_path);
            display_help(argv[0]);
        }

        if (!strcmp(arg, "-d") || !strcmp(arg, "--daemon")) {
            cfg->daemonize = 1;
            continue;
        }

        if (!strcmp(arg, "--log")) {
            cfg->show_log = 1;
            continue;
        }
        
        char *value = strchr(arg, '=');
        if (value == NULL) {
             fprintf(stderr, "error: Invalid argument or missing value for %s\n", arg);
             continue;
        }
        
        *value = '\0';
        value++;

        if (strcmp(arg, "--path") == 0) cfg->exec_path = strdup(value);
        else if (strcmp(arg, "--port") == 0) cfg->port = atoi(value);
        else if (strcmp(arg, "--workers") == 0) cfg->workers = atoi(value);
        else if (strcmp(arg, "--log-level") == 0) cfg->log_level = strdup(value);
        else fprintf(stderr, "error: Unknown argument: %s\n", arg);
    }
    if (!cfg->exec_path) cfg->exec_path = get_default_path();
}


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

config_t cfg; // global cfg file so it is inherited by workers processes

int main(int argc, char **argv) {
    parse_arguments(argc, argv, &cfg);

    if (cfg.show_log) {
        display_log_file();
        return(EXIT_SUCCESS);
    }
    if (cfg.daemonize) {
        printf("starting Caffeine server as a daemon...\n");
        daemonize();
    }
    
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
    if (listen(ipc_socket, cfg.workers) < 0) {
        perror("ipc listen");
        exit(EXIT_FAILURE);
    }
    
    // Array to hold the connected worker FDs
    int worker_fds[cfg.workers];

    // Fork the worker processes
    for (int i = 0; i < cfg.workers; i++) {
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

    printf("Parent accepting connections from %d workers...\n", cfg.workers);
    for (int i = 0; i < cfg.workers; i++) {
        worker_fds[i] = accept(ipc_socket, NULL, NULL);
        if (worker_fds[i] < 0) { perror("ipc accept"); exit(EXIT_FAILURE); }
        printf("Worker %d connected on FD %d.\n", i, worker_fds[i]);
    }
    close(ipc_socket);

    struct pollfd pfds[cfg.workers];
    for (int i = 0; i < cfg.workers; i++) {
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

        int flags = fcntl(client_fd, F_GETFD);
        if (flags != -1) fcntl(client_fd, F_SETFD, flags | FD_CLOEXEC);
        if (poll(pfds, cfg.workers, -1) < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            continue;
        }

        for (int i = 0; i < cfg.workers; i++) {
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
    for (int i = 0; i < cfg.workers; i++) close(worker_fds[i]);
    return 0;
}