#ifndef CAFFEINE_SIG_H
#define CAFFEINE_SIG_H

#include <signal.h>
#include <stdlib.h>

extern volatile sig_atomic_t g_shutdown_requested;

int sig_init();
void sigterm_handler(int signum);
void stop_server();

#endif