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
#define SOCK_FILE_PREFIX "caffeine_"
#define SOCK_FILE_SUFFIX ".sock"
#define LOG_PATH "/.local/share/caffeine/"
#define EXEC_PATH "/.config/caffeine/"
#define PID_PATH "/tmp/"
#define PID_FILE_PREFIX "caffeine_"
#define PID_FILE_SUFFIX ".pid"

int send_fd(int socket, int fd_to_send);
int recv_fd(int socket);
void exec_worker();

char* get_log_path();
void daemonize();

#endif