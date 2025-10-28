#ifndef CAFFEINESIG_H
#define CAFFEINESIG_H

#include <signal.h>
#include <stdlib.h>

extern volatile sig_atomic_t shutdown_requested;

void sigterm_handler(int signum);
void stop_server();

#endif