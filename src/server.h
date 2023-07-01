#define BUFFER_SIZE 1024
#define NUM_WORKERS 1

// FIXME: I don't know if these should stay here
static struct worker *workers;
static int inet_sock_fd;

void gracefully_shutdown(int n_workers, struct worker *workers);
