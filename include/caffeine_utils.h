#ifndef CAFFEINE_UTILS_H
#define CAFFEINE_UTILS_H

#include <caffeine.h>

void init_config();
char* trim_whitespace(char *str);
void free_and_exit(int status);
char* get_socket_path();
char* get_pid_path();
char* get_log_path();
char* get_default_path();
void list_running_instances();

#endif