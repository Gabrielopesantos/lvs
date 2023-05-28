#include "main.h"
#include "ipc.h"
#include "worker.h"
#include <errno.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

    fprintf(stdout, "Socket created and bound successfully.\n");

    return sockfd;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
                "ERROR: Expected bind port wasn't provided. Usage: "
                "%s --listen {port}.\n",
                argv[0]);
        exit(1);
    }

    // NOTE: static?
    struct worker *workers = calloc(NUM_WORKERS, sizeof(struct worker));
    spawn_workers(workers);

    char *port = argv[2];
    int sockfd = he_listen();

    // Accept connections and reply
    if (listen(sockfd, 5) == -1) {
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
            accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
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

        // handle_conn(conn_sockfd);
    }

    // Close the socket (Isn't closed not available anymore in sockets.h?)
    close(sockfd);

    exit(0);
}
