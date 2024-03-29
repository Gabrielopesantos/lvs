#include "main.h"
#include "ipc.h"
#include "thread_pool.h"
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

#define DEFAULT_PORT "8080"
#define DEFAULT_SERVE_DIRECTORY "."

struct worker *workers;
int inet_sockfd;

void shutdown_and_close_internet_socket() {
    // Gracefully shutdown TCP connection by sending FIN
    if (shutdown(inet_sockfd, SHUT_RDWR) == -1) {
        log_error("Failed to shutdown internet socket: %s", strerror(errno));
    }

    // Close internet listening socket
    if (close(inet_sockfd) == -1) {
        log_error("Failed to close internet socket: %s", strerror(errno));
    }
}

void gracefully_shutdown_worker_processes(int n_workers,
                                          struct worker *workers);

int socket_listen(uint16_t port) {
    int sockfd;
    struct sockaddr_in server_addr;

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        log_error("Could not create socket: %s", strerror(errno));
        return -1;
    }

    // Set up the server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    // Bind the socket to the server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
        -1) {
        log_error("Could not bind socket: %s", strerror(errno));
        return -1;
    }

    // Listen on the socket
    if (listen(sockfd, 5) == -1) {
        log_error("Could not listen on socket: %s", strerror(errno));
        return -1;
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
    log_info("Terminating process...");
    gracefully_shutdown_worker_processes(NUM_WORKERS, workers);
    shutdown_and_close_internet_socket();

    exit(EXIT_SUCCESS);
}

int dispatch_conn(int conn_sockfd) {
    // Pick a random worker
    // FIXME: Can get in this while loop
    int worker_index = rand() % NUM_WORKERS;
    int is_available = workers[worker_index].available;
    while (!is_available) {
        worker_index = (worker_index + 1) % NUM_WORKERS;
        is_available = workers[worker_index].available;
    }

    // Send connection socket to worker
    if (send_fd(workers[worker_index].ipc_sock, conn_sockfd) == -1) {
        return -1;
    }

    return worker_index;
}

void expect_worker_response(void *worker, int conn_sockfd) {
    struct worker *worker_ptr = (struct worker *)worker;
    char msg_buf[11];
    read(worker_ptr->ipc_sock, &msg_buf, sizeof(msg_buf));

    // FIXME: If worker fails to answer back, we consider it dead
    if (strcmp(msg_buf, "CONN_CLOSE") == 0) {
        // This var should be locked
        worker_ptr->available = 1;
        close(conn_sockfd);
    }
}

int main(int argc, char *argv[]) {
    char *port, *directory;
    switch (argc) {
    case 1:
        port = DEFAULT_PORT;
        directory = DEFAULT_SERVE_DIRECTORY;
        break;
    case 3:
        port = argv[2];
        directory = DEFAULT_SERVE_DIRECTORY;
        break;
    case 5:
        port = argv[2];
        directory = argv[4];
        break;
    default:
        if (argc < 5) {
            log_error("Invalid number of arguments provided. Usage: "
                      "%s --listen {port} --directory {current_dir}.",
                      argv[0]);
        } else {
            log_error("Too many arguments provided. Usage: "
                      "%s --listen {port} --directory {current_dir}.",
                      argv[0]);
        }
        exit(EXIT_FAILURE);
        break;
    }
    if (argc == 0) {
        log_error("Expected bind port wasn't provided. Usage: "
                  "%s --listen {port} --directory {current_dir}.",
                  argv[0]);
        exit(EXIT_FAILURE);
    }

    workers = calloc(NUM_WORKERS, sizeof(struct worker));
    spawn_workers(workers);

    inet_sockfd = socket_listen((uint16_t)atoi(port));
    if (inet_sockfd == -1) {
        log_error("Failed to setup listening internet socket.");
        goto exit_failure_close;
    }

    // Setup SIGINT handler to gracefully shutdown server
    trap_signal(SIGINT, sigint_handler);

    log_info("Server listening for connections...");

    threadpool_t threadpool;
    init_threadpool(&threadpool, NUM_WORKERS);

    struct sockaddr_in client_addr;
    socklen_t client_len;
    int conn_sockfd;
    while (1) {
        // Accept new connection
        client_len = sizeof(client_addr);
        conn_sockfd =
            accept(inet_sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (conn_sockfd == -1) {
            log_warn("Failed to accept new connection: %s", strerror(errno));
            continue;
        }

        // log_info("New client connection accepted");

        // Send connection socket to worker
        int worker_index;
        if ((worker_index = dispatch_conn(conn_sockfd)) == -1) {
            log_error("Failed to dispatch connection socket to a worker: %s",
                      strerror(errno));
            continue;
        }

        // Mark worker as unavailable
        workers[worker_index].available = 0;

        // Expect a response from worker
        submit_job(&threadpool, expect_worker_response, &workers[worker_index],
                   conn_sockfd);
    }

    shutdown(inet_sockfd, SHUT_RDWR);
    close(inet_sockfd);
    shutdown_threadpool(&threadpool);
exit_failure_close:
    gracefully_shutdown_worker_processes(NUM_WORKERS, workers);
    exit(EXIT_FAILURE);
}

void gracefully_shutdown_worker_processes(int n_workers,
                                          struct worker *workers) {
    // Terminate workers
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (workers[i].up == 1) {
            // TODO: Add a signal hander on worker to free resources
            if (kill(workers[i].pid, SIGINT) == 0) {
                log_info("Worker with PID %d successfully terminated",
                         workers[i].pid);
            }
        }
    }

    // Free allocated memory for workers
    free(workers);
}

// FIXME: Move from `main`
void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stdout, "INFO: ");
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    va_end(args);
}

void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "WARN: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}
