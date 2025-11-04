#include <caffeine.h>
#include <caffeine_sig.h>
#include <caffeine_utils.h>
#include <caffeine_cfg.h>
#include <log.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>

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
        if (errno == ECONNRESET) {
            LOG_DEBUG("recvmsg: Parent closed IPC socket (ECONNRESET). Signaling worker exit.");
            return -1; 
        }
        
        LOG_ERROR("recvmsg: %s", strerror(errno));
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        return *(int *)CMSG_DATA(cmsg);
    }
    return -1;
}

void server_loop(int listen_fd, int *worker_fds) {
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
}

int main(int argc, char **argv) {
    // 1. INITIAL SETUP
    init_config();

    // Store arguments for later management commands
    g_cfg.argc = argc;
    g_cfg.argv = argv;

    if (sig_init() < 0) {
        LOG_ERROR("Failed to initialize signals: %s", strerror(errno));
    }
    
    if (parse_arguments(argc, argv) < 0) {
        free_and_exit(EXIT_FAILURE);
    }
    
    // --- Management Logic Placeholder ---
    // Note: Management commands like -s, -L, -l would be handled here 
    // before the server starts. We skip that implementation for core startup.
    // ...

    // 2. SETUP SHARED TCP LISTENER (listen_fd)
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("socket: %s", strerror(errno));
        free_and_exit(EXIT_FAILURE);
    }

    int enable = 1;
    
    // Set SO_REUSEADDR
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        LOG_ERROR("setsockopt(SO_REUSEADDR): %s", strerror(errno));
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }

    // CRITICAL: Set SO_REUSEPORT. This allows all workers to bind to the same port.
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
        // Log a warning, but don't fail, as some old kernels may not support it.
        LOG_WARN("setsockopt(SO_REUSEPORT) failed. Connection dispatch may be less efficient: %s", strerror(errno));
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cfg.port);
    
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("Failed to bind to port %d: %s", g_cfg.port, strerror(errno));
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }
    
    // The backlog is generally set higher for high-volume servers
    if (listen(listen_fd, 4096) < 0) {
        LOG_ERROR("Couldn't listen on port %d: %s", g_cfg.port, strerror(errno));
        close(listen_fd);
        free_and_exit(EXIT_FAILURE);
    }

    // 3. FORK WORKER PROCESSES
    LOG_INFO("Parent is now the process manager (PID %d).", getpid());
    LOG_INFO("Spawning %d worker processes...", g_cfg.workers);
    
    // array to hold the connected worker PIDs (for process management)
    pid_t worker_pids[g_cfg.workers]; 

    for (int i = 0; i < g_cfg.workers; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            LOG_ERROR("fork failed during worker spawn: %s", strerror(errno));
            // In a real scenario, we would clean up existing children here.
            free_and_exit(EXIT_FAILURE);
        }
        
        if (pid == 0) {
            // CHILD/WORKER PROCESS
            LOG_INFO("Worker process started (PID %d).", getpid());
            
            // The worker takes ownership of the listener FD (listen_fd).
            // It will call accept() directly on it in its execution loop.
            exec_worker(listen_fd);
            
            // exec_worker should ideally never return, but if it does, exit.
            exit(EXIT_FAILURE); 
        } 

        // PARENT PROCESS
        worker_pids[i] = pid;
    }
    
    // CRITICAL: Since the workers are now responsible for accepting connections,
    // the parent closes its own copy of the listening file descriptor.
    // The parent now only manages the child processes.
    close(listen_fd); 
    LOG_DEBUG("Parent closed its copy of listen_fd.");
    
    // 4. DAEMONIZATION (IF REQUESTED)
    if (g_cfg.daemonize) {
        LOG_INFO("Starting Caffeine server as a daemon...");
        daemonize();
    }
    
    LOG_INFO("Caffeine server running with %d workers on port %d.", g_cfg.workers, g_cfg.port);
    
    // 5. PARENT PROCESS MANAGEMENT LOOP
    // This loop prevents the parent from exiting, allowing it to manage and
    // potentially respawn workers (though this is a simplified loop).
    int status;
    pid_t child_pid;

    while (1) {
        // Wait for any child process to change state (e.g., terminate)
        child_pid = waitpid(-1, &status, 0); 

        if (child_pid < 0) {
            if (errno == EINTR) continue; // Interrupted by a signal, loop again
            if (errno == ECHILD) { 
                // No more children left (all workers died), critical failure
                LOG_ERROR("All worker processes died. Parent is shutting down.");
                break;
            }
            LOG_ERROR("waitpid error: %s", strerror(errno));
            break;
        }

        LOG_WARN("Worker PID %d terminated (Status: %d). Respawn logic would go here.", child_pid, status);
        // NOTE: A production server would attempt to fork a new worker here.
    }
    
    // 6. CLEANUP (Only reached if the main loop breaks)
    LOG_INFO("Parent performing cleanup and exiting.");
    // In a full cleanup, we would send SIGTERM to all remaining workers here.
    free_and_exit(EXIT_SUCCESS);
    return 0;
}