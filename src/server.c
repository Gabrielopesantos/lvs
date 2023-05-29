#include "server.h"
#include "worker.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
// #include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>

void gracefully_shutdown(int n_workers, struct worker *workers) {
    // Terminate workers
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (workers[i].available == 1) {
            if (kill(workers[i].pid, SIGINT) == 0) {
                printf("Worker with PID %d successfully terminated\n",
                       workers[i].pid);
            }
        }
    }

    // Free allocated memory for workers
    free(workers);

    // Close internet connection socket
    close(inet_sock_fd);

    exit(EXIT_SUCCESS);
}
