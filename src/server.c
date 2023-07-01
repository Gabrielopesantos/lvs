#include "server.h"
#include "worker.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
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
}
