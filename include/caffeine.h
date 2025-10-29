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

#define SOCKET_PATH "/tmp/"
#define DEFAULT_WORKERS 4
#define DEFAULT_PORT 8080
#define DEFAULT_LOG_LEVEL "INFO"

#define PID_FILE "/tmp/"
#define LOG_FILE "/.local/share/caffeine/"

typedef struct {
    int     port;
    int     workers;
    int     daemonize;
    int     show_log;
    int     reset_log;
    int     stop_server;
    int     instance_list;
    char    *instance_name;
    char    *exec_path;
    char    *log_level;
    char    *socket_path;
    char    *log_path;
    char    *pid_path;
}   config_t;

extern config_t g_cfg;

int send_fd(int socket, int fd_to_send);
int recv_fd(int socket);
void exec_worker();

char* get_log_path();
void daemonize();

#endif