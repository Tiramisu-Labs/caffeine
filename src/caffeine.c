#include <caffeine.h>
#include <caffeine_sig.h>
#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <log.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>

void display_help() {
    printf("Usage: %s [OPTIONS]\n", "caffeine");
    printf("Caffeine: A high-performance pre-fork web server.\n\n");
    printf("Options:\n");
    printf("  -h, --help                Display this help message and exit.\n");
    printf("  -d, --daemon <name>       Run the server as a background daemon and associate it with an instance name.\n");
    printf("  --instance-list           Show all running instances.\n");
    printf("  --path=<path>             Set the base path for executable handlers (Default: ~/.config/caffeine/).\n");
    printf("  --port=<port>             Set the listening port (Default: %d).\n", DEFAULT_PORT);
    printf("  --workers=<num>           Set the number of worker processes (Default: %d).\n", DEFAULT_WORKERS);
    printf("  --log-level=<level>       Set logging verbosity (DEBUG, INFO, WARN, ERROR) (Default: %s).\n", DEFAULT_LOG_LEVEL);
    printf("  -l, --log <name>          Show logs file\n");
    printf("  --reset-logs              Clear the log file\n");
    printf("  --stop <name>             Stop the corresponding server and all the workers\n");
    printf("  --instance-list           List all running instances\n");
}

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <log.h>

void display_log_file() {
    int fd = open(get_log_path(), O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("Error: Could not open log file at %s: %s\n", get_log_path(), strerror(errno));
        return;
    }

    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        if (write(STDOUT_FILENO, buffer, bytes_read) < 0) {
            LOG_ERROR("Error writing to stdout: %s", strerror(errno));
            break;
        }
    }
    close(fd);
}

void reset_log_file() {
    int fd = open(get_log_path(), O_WRONLY | O_TRUNC);
    if (fd < 0) {
        LOG_ERROR("Error: Could not open log file at %s: %s\n", get_log_path(), strerror(errno));
        return;
    }
    close(fd);
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
        LOG_ERROR("sendmsg: %s", strerror(errno));
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
        LOG_ERROR("recvmsg: %s", strerror(errno));
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        return *(int *)CMSG_DATA(cmsg);
    }
    return -1;
}

int main(int argc, char **argv) {
    init_config();
    set_log_level(g_cfg.log_level);
    if (sig_init() < 0) {
        LOG_ERROR("failed to init signals: %s", strerror(errno));
    }
    if (parse_arguments(argc, argv) < 0) {
        free_and_exit(EXIT_FAILURE);
    }

    if (g_cfg.show_log) {
        display_log_file();
        free_and_exit(EXIT_SUCCESS);
    }
    if (g_cfg.reset_logs) {
        reset_log_file();
        free_and_exit(EXIT_SUCCESS);
    }
    
    if (g_cfg.stop_instance) {
        stop_server();
        free_and_exit(EXIT_SUCCESS);
    }

    if (g_cfg.list_instances) {
        list_running_instances();
        free_and_exit(EXIT_SUCCESS);
    }
    char *socket_path = get_socket_path();

    unlink(socket_path);
    
    int ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_socket < 0) {
        LOG_ERROR("ipc socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un ipc_addr;
    memset(&ipc_addr, 0, sizeof(ipc_addr));
    ipc_addr.sun_family = AF_UNIX;
    strncpy(ipc_addr.sun_path, socket_path, sizeof(ipc_addr.sun_path) - 1);
    if (bind(ipc_socket, (struct sockaddr *)&ipc_addr, sizeof(ipc_addr)) < 0) {
        LOG_ERROR("ipc bind: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (listen(ipc_socket, g_cfg.workers) < 0) {
        LOG_ERROR("ipc listen: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // Array to hold the connected worker FDs
    int worker_fds[g_cfg.workers];
    pid_t worker_pids[g_cfg.workers];
    // Fork the worker processes
    for (int i = 0; i < g_cfg.workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERROR("fork: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            close(ipc_socket);
            exec_worker();
        } // Pass the listening socket to the worker
        worker_pids[i] = pid;
    }
    LOG_DEBUG("Parent finished forking %d workers.", g_cfg.workers);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cfg.port);
    
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to bind to port %d: %s", g_cfg.port, strerror(errno));
        free_and_exit(EXIT_FAILURE);
    }
    if (listen(listen_fd, 10) < 0) {
        LOG_ERROR("Couldn't listen on port %d...", g_cfg.port);
        free_and_exit(EXIT_FAILURE);
    }

    if (g_cfg.daemonize) {
        LOG_INFO("starting Caffeine server as a daemon...");
        daemonize();
    }
    
    LOG_INFO("Parent listening for web requests on port %d...", g_cfg.port);

    LOG_INFO("Parent accepting connections from %d workers...", g_cfg.workers);
    for (int i = 0; i < g_cfg.workers; i++) {
        LOG_DEBUG("accepting worker n %d", i);
        worker_fds[i] = accept(ipc_socket, NULL, NULL);
        if (worker_fds[i] < 0) {
            LOG_ERROR("ipc accept: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        LOG_DEBUG("Parent accepted IPC connection from worker %d on FD %d.", i, worker_fds[i]);
        LOG_INFO("Worker %d connected on FD %d.", i, worker_fds[i]);
    }
    close(ipc_socket);

    struct pollfd pfds[g_cfg.workers];
    for (int i = 0; i < g_cfg.workers; i++) {
        pfds[i].fd = worker_fds[i];
        pfds[i].events = POLLIN;
    }
    
    while (1) {
        if (g_shutdown_requested) {
            break;
        }
        LOG_DEBUG("Parent loop start. Listening for new client on FD %d.", listen_fd);
        int client_fd = accept(listen_fd, NULL, NULL);
        LOG_DEBUG("Parent accepted client FD %d.", client_fd);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("accept: %s", strerror(errno));
            continue;
        }

        int flags = fcntl(client_fd, F_GETFD);
        if (flags != -1) fcntl(client_fd, F_SETFD, flags | FD_CLOEXEC);
        LOG_DEBUG("Parent polling workers for readiness...");
        if (poll(pfds, g_cfg.workers, -1) < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("poll: %s", strerror(errno));
            continue;
        }
        LOG_DEBUG("Poll returned. Checking for ready worker for client FD %d.", client_fd);
        for (int i = 0; i < g_cfg.workers; i++) {
            int worker_ipc_fd = worker_fds[i];
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                char ready_byte;
                ssize_t bytes_read = read(pfds[i].fd, &ready_byte, 1);
                
                if (bytes_read <= 0) {
                    LOG_ERROR("Worker %d disconnected. Reaping.\n", i);
                    // TODO: respawn the worker here
                    break; 
                }

                send_fd(pfds[i].fd, client_fd);
                LOG_DEBUG("Parent sent FD %d to worker %d.", client_fd, i);
                LOG_INFO("Dispatched client FD %d to ready worker %d.", client_fd, i);
                close(client_fd);
                break;
            }
        }
    }
    LOG_INFO("Parent performing cleanup and exiting.");

    close(listen_fd);
    for (int i = 0; i < g_cfg.workers; i++) close(worker_fds[i]);
    unlink(socket_path);
    remove(socket_path);
    free_and_exit(EXIT_SUCCESS);
    return 0;
}