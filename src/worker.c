#include "worker.h"
#include "http_parser.h"
#include "ipc.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_URL_SIZE 256

struct connection {
    int fd;
    int completed;
    int buf_size;
    char *buf;
    char *url;
};

// FIXME: Use `static`

// FIXME: Errors
struct connection *new_connection(int conn_fd) {
    struct connection *conn = malloc(sizeof(struct connection));
    if (conn == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate memory for connection\n");
        exit(EXIT_FAILURE);
    }

    conn->completed = 0;
    conn->fd = conn_fd;

    // conn->buf = malloc(MAX_BUFFER_SIZE);
    conn->buf = calloc(MAX_BUFFER_SIZE, sizeof(char));
    if (conn->buf == NULL) {
        free(conn);
        fprintf(stderr, "ERROR: Failed to allocate memory for buffer\n");
        exit(EXIT_FAILURE);
    }
    conn->buf_size = MAX_BUFFER_SIZE;

    conn->url = calloc(MAX_URL_SIZE, sizeof(char));
    if (conn->url == NULL) {
        free(conn);
        free(conn->buf);
        fprintf(stderr, "ERROR: Failed to allocate memory for url\n");
        exit(EXIT_FAILURE);
    }

    return conn;
}

void free_connection(struct connection *conn) {
    free(conn->buf);
    free(conn->url);
    free(conn);
}

// Callback when a URL is found in the request
int on_url_cb(http_parser *parser, const char *url, size_t url_len) {
    struct connection *conn = parser->data;
    printf("URL found: %.*s\n", (int)url_len, url); // FIXME: remove
    memcpy(conn->url, url, url_len);

    return 0;
}
int on_message_complete_cb(http_parser *parser) {
    struct connection *conn = parser->data;
    conn->completed = 1;

    return 0;
}

int read_request(http_parser *parser, struct connection *conn) {
    ssize_t msg_size;

    msg_size = recv(conn->fd, conn->buf, conn->buf_size, 0);
    if (msg_size == -1) {
        perror("recv");
        return -1;
    }
    fprintf(stdout, "Received the message\n'%s'\nwith size %zd\n", conn->buf,
            msg_size);

    // FIXME: Think where to put these settings
    http_parser_settings settings = {0};
    // memset(&settings, 0, sizeof(settings));
    settings.on_url = on_url_cb;
    settings.on_message_complete = on_message_complete_cb;

    printf("Before parsing request\n");

    size_t parsed = http_parser_execute(parser, &settings, conn->buf, msg_size);
    if (parsed != msg_size) {
        printf("Failed to parse request: %s\n",
               http_errno_description(HTTP_PARSER_ERRNO(parser)));
    }

    return 0;
}

int send_response(struct connection *conn) { return 0; }

// Handle the new connection (send/receive data)
// NOTE: For now we just want to send back exactly what we receive
int handle_connection(int conn_sockfd, http_parser *parser) {
    // FIXME: Handle error
    struct connection *conn = new_connection(conn_sockfd);

    parser->data = conn;

    if (read_request(parser, conn) < 0) {
        close(conn_sockfd);
        free_connection(conn);
    }

    if (send_response(conn) < 0) {
        close(conn_sockfd);
        free_connection(conn);
    }

    free_connection(conn);
    shutdown(conn_sockfd, SHUT_RDWR);
    close(conn_sockfd);
    return 0;
}

void worker_loop(int recv_sockfd) {
    // printf("recv socket fd %d\n", recv_sockfd);

    // NOTE: What if this was moved to the struct?
    // FIXME: Handle malloc failure
    http_parser *parser = malloc(sizeof(http_parser));
    http_parser_init(parser, HTTP_REQUEST);

    while (1) {
        int conn_sockfd;
        if (receive_fd(recv_sockfd, &conn_sockfd) < 0) {
            fprintf(stderr,
                    "ERROR: Failed to read file descriptor from socket\n");
            // FIXME: It might mean that the parent process wasn't successfully
            // initialized. For now terminate the worker when this happens.
            exit(EXIT_FAILURE);
        } else {
            printf("Message received, connection socket fd: %d\n", conn_sockfd);
            if (handle_connection(conn_sockfd, parser) < 0) {
                fprintf(stderr, "ERROR: failed to handle connection\n");
            }
        }
        // break; // FIXME: Remove this break
    }

    fprintf(stdout, "Closing worker processing\n");
    free(parser);
    exit(EXIT_SUCCESS); // NOTE: Exiting exec for now;
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
        printf("Worker process spawned. PID: %d\n", pid);
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

void spawn_workers(struct worker *workers) {
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (new_worker(&workers[i]) == -1) {
            // NOTE: Let's not think about retries for now
            fprintf(stderr, "ERROR: Failed to spawn worker with id %d\n", i);
        }
    }
}
