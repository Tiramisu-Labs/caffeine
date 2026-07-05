#ifndef SIG_H
#define SIG_H

#include <signal.h>
#include <stdlib.h>

extern volatile sig_atomic_t g_shutdown_requested;

void sigterm_handler(int signum);
void stop_server();

#endif