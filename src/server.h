// #include "worker.h"

#define BUFFER_SIZE 1024
#define NUM_WORKERS 1

static struct worker *workers;
static int inet_sock_fd;

void gracefully_shutdown(int n_workers, struct worker *workers);
