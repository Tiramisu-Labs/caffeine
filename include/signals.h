#ifndef SIGNALS_H
#define SIGNALS_H

#include <signal.h>

extern volatile sig_atomic_t g_shutdown_requested;

#endif