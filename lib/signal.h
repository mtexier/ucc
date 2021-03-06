#ifndef __SIGNAL_H
#define __SIGNAL_H

#include "unistd.h"
#include "sys/signal.h"

int kill(pid_t pid, int sig);
int raise(int);

#endif
