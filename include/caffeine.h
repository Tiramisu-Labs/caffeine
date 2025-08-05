#ifndef CAFFEINE_H
#define CAFFEINE_H

typedef enum {
    OK,
    RDEND,
    FD_ERROR
}   recv_status;

int handle_client_data(int sender);

#endif