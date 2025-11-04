#ifndef HEADERS_H
#define HEADERS_H

#include <caffeine.h>

int read_headers(int client_fd, headers_t *hdrs);
int check_valid_path(int client_fd, char *full_path);
int setup_cgi_environment(headers_t *hdrs, int max_strings, int max_length, char env_buffer[max_strings][max_length], char **envp);

#endif