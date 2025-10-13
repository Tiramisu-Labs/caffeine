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

#define SOCKET_PATH "/tmp/webserver.sock"
#define WORKER_COUNT 4

int send_fd(int socket, int fd_to_send);
int recv_fd(int socket);
void exec_worker();

#endif