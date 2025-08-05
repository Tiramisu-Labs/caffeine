#include "../include/caffeine.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <string.h>

char* get_path(const char *str)
{
    int i = 0;
    int size = 0;
    int offset = 0;
    while (str[i]) {
        if (str[i] == '/') {
            offset = i;
            while (str[i] != ' ') {
                size++;
                i++;
            }
            break;
        }
        i++;
    }
    char *ret = malloc(size + 1);
    memcpy(ret, &str[offset], size);
    ret[size] = 0;
    return ret;
}

int handle_client_data(int sender)
{
    char buf[8192];
    int nbytes = recv(sender, buf, sizeof buf, 0);

    if (nbytes <= 0) {
        if (nbytes == 0) printf("pollserver: socket %d hung up\n", sender);
        else perror("recv");

        return (RDEND);
    } else {
        printf("pollserver: recv from fd %d:\n%.*s", sender, nbytes, buf);
        char *path = get_path(buf);
        printf("path %s\n", path);
        pid_t parent = getpid();
        char pipe_buff[8192 * 2];
        int pipes[2];
        pipe(pipes);
        pid_t pid = fork();
        if (pid == -1) {
            // error, failed to fork()
        } else if (pid > 0) {
            close(pipes[1]);
            int status;
            waitpid(pid, &status, 0);
            nbytes = read(pipes[0], pipe_buff, sizeof(pipe_buff));
            printf("nbytes %d output %s\n", nbytes, pipe_buff);
        } else {
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);
            // parse buffer and get path

            // we are the child
            execl("/usr/bin/ls", "ls", "-1", (char *)0);
            _exit(EXIT_FAILURE);   // exec never returns
        }
    }
}