#include "http_parser.h"

struct worker {
    int pid;
    int ipc_sock;
    int available;
};

void spawn_workers(struct worker *workers);
int new_worker(struct worker *w);
void worker_loop(int ipc_sock_fd);
int handle_conn(int conn_sockfd, http_parser *parser);
