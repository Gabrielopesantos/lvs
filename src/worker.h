#ifndef WORKER_H
#define WORKER_H

#include "http_parser.h"
#include <unistd.h>

struct worker {
    pid_t pid;
    int ipc_sock;
    int available;
    int up;
};

void spawn_workers(struct worker *workers);

#endif // WORKER_H
