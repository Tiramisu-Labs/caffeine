#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <arpa/inet.h>
#include <signal.h>

#define SOCKET_PATH "/tmp/webserver.sock"
#define DEFAULT_WORKERS 4
#define DEFAULT_PORT 8080
#define DEFAULT_LOG_LEVEL "INFO"

#define PID_FILE "/tmp/.caffeine.pid"
#define LOG_FILE "$HOME/.local/share/caffeine/caffeine.log"

typedef struct {
    int port;
    int workers;
    int daemonize;
    int show_log;
    int reset_log;
    int stop_server;
    char *exec_path;
    char *log_level;
}   config_t;

extern config_t cfg;

int send_fd(int socket, int fd_to_send);
int recv_fd(int socket);
void exec_worker();

char* get_log_path();
void daemonize();

#endif