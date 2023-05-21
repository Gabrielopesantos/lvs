#include "worker.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

void spawn_workers(struct worker *workers) {
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (new_worker(&workers[i]) == -1) {
            // NOTE: Let's not think about retries for now
            fprintf(stderr, "ERROR: Failed to spawn worker with id %d", i);
        }
    }
}

int new_worker(struct worker *w) {
    int pid, ipc_sock_pair[2];
    // NOTE: What's the automated way of chosing the PROTOCOL?
    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, ipc_sock_pair) == -1) {
        return -1;
    }

    pid = fork();
    if (pid == 0) {
        // Child process
        close(ipc_sock_pair[0]);
        worker_loop(ipc_sock_pair[1]);
    } else if (pid > 0) {
        // fprintf(stdout, "Child process forked with pid: %d\n", pid);
        close(ipc_sock_pair[1]);
        // Parent process
        w->pid = pid;
        w->ipc_sock = ipc_sock_pair[0];
        w->available = 1;
    } else {
        perror("fork");
    }

    return 0;
}

void worker_loop(int ipc_sock_fd) {
    // printf("IPC socket fd %d\n", ipc_sock_fd);

    exit(0); // NOTE: Exiting exec for now;
}
