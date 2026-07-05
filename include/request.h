#ifndef REQUEST_H
#define REQUEST_H

#include <caffeine.h>

bool read_headers(int client_fd, headers_t *hdrs);
bool read_body(int client_fd, headers_t *hdrs);

#endif