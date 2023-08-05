#include "main.h"
#include "ipc.h"
#include "worker.h"
#include <errno.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void gracefully_shutdown(int n_workers, struct worker *workers);

struct worker *workers;
int inet_sock_fd;

// NOTE: Rename function once I understand what exactly is happening here
// How do I want to handle *possible* errors, let fail the program? Yes, for
// now.
int he_listen(void) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set up the server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8080);

    // Bind the socket to the server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
        -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

void trap_signal(int sig, void (*sig_handler)(int)) {
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);

    sa.sa_handler = sig_handler;
    sa.sa_flags = 0;
#ifdef SA_RESTART
    sa.sa_flags |= SA_RESTART;
#endif

    if (sigaction(sig, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }
}

void sigint_handler(int s) {
    printf("Terminating...\n");

    // Terminate worker processes
    gracefully_shutdown(NUM_WORKERS, workers);

    // Close internet listening socket
    if (close(inet_sock_fd) == -1) {
        perror("close");
    } else {
        // TMP
        fprintf(stdout, "Socket %d closed\n", inet_sock_fd);
    }

    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
                "ERROR: Expected bind port wasn't provided. Usage: "
                "%s --listen {port}.\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    workers = calloc(NUM_WORKERS, sizeof(struct worker));
    spawn_workers(workers);

    // FIXME: There are cases where the bind fails and workers have already been
    // spawned
    char *port = argv[2];
    inet_sock_fd = he_listen();

    // Handle signals
    trap_signal(SIGINT, sigint_handler);

    // Accept connections and reply
    if (listen(inet_sock_fd, 5) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Server listening for connections...\n");

    struct sockaddr_in client_addr;
    socklen_t client_len;
    int conn_sockfd;
    while (1) {
        // Accept new connection
        client_len = sizeof(client_addr);

        conn_sockfd =
            accept(inet_sock_fd, (struct sockaddr *)&client_addr, &client_len);
        if (conn_sockfd == -1) {
            perror("accept");
            continue;
        }

        fprintf(stdout, "New client connection accepted.\n");

        // NOTE: Commented while we can't get IPC between sender and consumer
        // processes to work
        if (send_fd(workers[0].ipc_sock, conn_sockfd) == -1) {
            fprintf(stderr, "ERROR: Failed to send connection socket: %s\n",
                    strerror(errno));
            continue; // NOTE: continue?
        }

        // NOTE: Is this process also supposed to close the connection socket?
        // Validate.
        close(conn_sockfd);
    }

    // Close internet listening socket
    close(inet_sock_fd);

    exit(EXIT_SUCCESS);
}

void gracefully_shutdown(int n_workers, struct worker *workers) {
    // Terminate workers
    for (int i = 0; i < 1; i++) {
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
