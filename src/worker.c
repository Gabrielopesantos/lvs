#include "worker.h"
#include "ipc.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void spawn_workers(struct worker *workers) {
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (new_worker(&workers[i]) == -1) {
            // NOTE: Let's not think about retries for now
            fprintf(stderr, "ERROR: Failed to spawn worker with id %d\n", i);
        }
    }
}

int new_worker(struct worker *w) {
    pid_t pid;
    int ipc_sock_pair[2];
    // NOTE: What's the automated way of chosing the PROTOCOL?
    if (socketpair(AF_LOCAL, SOCK_STREAM, 0, ipc_sock_pair) == -1) {
        return 1;
    }

    pid = fork();
    if (pid == 0) {
        // Child process
        close(ipc_sock_pair[0]);
        worker_loop(ipc_sock_pair[1]);
    } else if (pid > 0) {
        printf("Child process forked with pid: %d\n", pid);
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

void worker_loop(int recv_sockfd) {
    printf("recv socket fd %d\n", recv_sockfd);

    while (1) {
        int conn_sockfd;
        if (receive_fd(recv_sockfd, &conn_sockfd) == -1) {
            fprintf(stderr, "ERROR: Failed to read fd from socket\n");
            // continue;
        } else {
            printf("===================\n");
            printf("Message received, connection socket fd: %d\n", conn_sockfd);
            printf("===================\n");
            if (handle_conn(conn_sockfd) == 1) {
                fprintf(stderr, "ERROR: failed to handle connection\n");
            }
        }
        // break; // FIXME: Remove this break
    }

    exit(0); // NOTE: Exiting exec for now;
}

int handle_conn(int conn_sockfd) {
    // Handle the new connection (send/receive data)
    // NOTE: For now we just want to send back exactly what we receive
    char msg_buf[BUFFER_SIZE];
    ssize_t msg_size;
    msg_size = recv(conn_sockfd, &msg_buf, BUFFER_SIZE, 0);
    if (msg_size == -1) {
        perror("recv");
        close(conn_sockfd);
        return 1;
    }
    fprintf(stdout, "Received the message '%s' with size %zd\n", msg_buf,
            msg_size);

    // Write back
    if (send(conn_sockfd, &msg_buf, msg_size, 0) == -1) {
        perror("send");
        close(conn_sockfd);
        return 1;
    }
    fprintf(stdout, "Message sent back to the client.\n");

    close(conn_sockfd);
    return 0;
}
