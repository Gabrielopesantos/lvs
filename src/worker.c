#include "worker.h"
#include "dirent.h"
#include "http_parser.h"
#include "ipc.h"
#include "main.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_URL_SIZE 256
#define MAX_FILE_PATH 512
#define MAX_RESPONSE_SIZE 12288

enum FileType { REGULAR, DIRECTORY, UNKNOWN };

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
        log_error("Failed to allocate memory for connection: %s",
                  strerror(errno));
        exit(EXIT_FAILURE); // FIXME:
    }

    conn->completed = 0;
    conn->fd = conn_fd;

    // conn->buf = malloc(MAX_BUFFER_SIZE);
    conn->buf = calloc(MAX_BUFFER_SIZE, sizeof(char));
    if (conn->buf == NULL) {
        free(conn);
        log_error("Failed to allocate memory for buffer: %s", strerror(errno));
        exit(EXIT_FAILURE); // FIXME:
    }
    conn->buf_size = MAX_BUFFER_SIZE;

    conn->url = calloc(MAX_URL_SIZE, sizeof(char));
    if (conn->url == NULL) {
        free(conn);
        free(conn->buf);
        log_error("Failed to allocate memory for url: %s", strerror(errno));
        exit(EXIT_FAILURE); // FIXME
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
    // Parse URL, remove any query parameters (?key=val) and/or fragment
    // identifiers (#fragment)
    for (int i = 0; i < url_len; i++) {
        if (url[i] == '?' || url[i] == '#') {
            url_len = i;
            break;
        }
    }
    strncpy(conn->url, url, url_len);

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

    // FIXME: Think where to put these settings
    http_parser_settings settings = {0};
    // memset(&settings, 0, sizeof(settings));
    settings.on_url = on_url_cb;
    settings.on_message_complete = on_message_complete_cb;

    size_t parsed = http_parser_execute(parser, &settings, conn->buf, msg_size);
    if (parsed != msg_size) {
        log_error("Failed to parse request: %s\n",
                  http_errno_description(HTTP_PARSER_ERRNO(parser)));
    }

    return 0;
}

int write_response(struct connection *conn, int status_code,
                   const char *additional_header[], char *body) {
    int response_size = 0, additional_header_size = 0;
    char *status_line, *response;
    switch (status_code) {
    case 200:
        status_line = "HTTP/1.1 200 OK";
        break;
    case 400:
        status_line = "HTTP/1.1 400 Bad Request";
        break;
    case 404:
        status_line = "HTTP/1.1 404 Not Found";
        break;
    case 500:
        status_line = "HTTP/1.1 500 Internal Server Error";
        break;
    case 501:
        status_line = "HTTP/1.1 501 Not Implemented";
        break;
    default:
        log_error("Invalid status code provided");
        return -1;
    }

    log_info("Response status line: %s | Path: %s", status_line, conn->url);

    response_size += strlen(status_line) + 2;
    // Check if additional headers and body have been provided
    for (int i = 0; additional_header[i] != NULL; i++) {
        additional_header_size += strlen(additional_header[i]) + 2;
    }
    response_size += additional_header_size;

    // Allocate 50 bytes to store the content length header
    char buffer[50];
    char *content_len_header = buffer;
    if (body != NULL) {
        int content_len = strlen(body);
        response_size += content_len;

        sprintf(content_len_header, "Content-Length: %d", content_len);
        // +2 for \r\n
        response_size += strlen(content_len_header) + 2;
    }
    response_size += 2;

    response = calloc(response_size, sizeof(char));
    if (response == NULL) {
        log_error("Failed to allocate memory for response");
        return -1;
    }

    if (body == NULL && additional_header[0] == NULL) {
        sprintf(response, "%s\r\n", status_line);
    } else {
        // char *connection_close_header = "Connection: close\r\n";
        sprintf(response, "%s\r\n", status_line);

        for (int i = 0; additional_header[i] != NULL; i++) {
            strcat(response, additional_header[i]);
            strcat(response, "\r\n");
        }

        if (body != NULL) {
            strcat(response, content_len_header);
            strcat(response, "\r\n");
            strcat(response, "\r\n");
            strcat(response, body);
        }
    }

    // FIXME: In a while loop? while (bytes_sent > 0)
    ssize_t bytes_sent = send(conn->fd, response, response_size, 0);
    free(response);
    if (bytes_sent == -1) {
        log_error("Could not send response: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int determine_request_file_type(char *path, enum FileType *file_type) {
    *file_type = UNKNOWN;

    struct stat file_stat;
    int exist = stat(path, &file_stat);
    if (exist == 0) {
    } else {
        // FIXME: missing != permissions
        log_warn("Path %s doesn't exist or there was some error validating "
                 "its existance: %s",
                 path, strerror(errno));
        return -1;
    }

    if (S_ISREG(file_stat.st_mode)) {
        *file_type = REGULAR;
    } else if (S_ISDIR(file_stat.st_mode)) {
        *file_type = DIRECTORY;
    }

    return 0;
}

int generate_directory_listing_html(const char *dir_path, char *output_buffer,
                                    size_t buffer_size) {
    DIR *d;
    struct dirent *dir;
    struct stat st;
    size_t written = 0;

    d = opendir(dir_path);
    if (d == NULL) {
        log_error("Unable to open directory '%s': %s", dir_path,
                  strerror(errno));
        return -1;
    }

    written += snprintf(output_buffer, buffer_size,
                        "<!DOCTYPE HTML>\n"
                        "<html lang=\"en\">\n"
                        "<head>\n"
                        "<meta charset=\"utf-8\">\n"
                        "<title>Directory listing for %s</title>\n"
                        "</head>\n"
                        "<body>\n"
                        "<h1>Directory listing for %s</h1>\n"
                        "<hr>\n"
                        "<ul>\n",
                        dir_path, dir_path);

    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue; // skip "." and ".."

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir->d_name);
        stat(full_path, &st);

        // Append to the output buffer
        if (S_ISDIR(st.st_mode)) {
            // It's a directory
            written += snprintf(output_buffer + written, buffer_size - written,
                                "<li><a href=\"%s/\">%s/</a></li>\n",
                                dir->d_name, dir->d_name);
        } else if (S_ISREG(st.st_mode)) {
            // It's a file
            written += snprintf(output_buffer + written, buffer_size - written,
                                "<li><a href=\"%s\">%s</a></li>\n", dir->d_name,
                                dir->d_name);
        } else {
            // Ignore other file types
            continue;
        }
    }
    closedir(d);

    written += snprintf(output_buffer + written, buffer_size - written,
                        "</ul>\n"
                        "<hr>\n"
                        "</body>\n"
                        "</html>\n");

    return 0;
}

int send_response(struct connection *conn) {
    if (conn->url == NULL) {
        log_error("Cannot send response. URL not set.");
        return -1;
    }

    char url_path[MAX_FILE_PATH];
    // FIXME: Wire serve_directory so it is available here
    snprintf(url_path, MAX_FILE_PATH, "%s%s", ".", conn->url);
    enum FileType file_type;
    if (determine_request_file_type(url_path, &file_type) == -1) {
        return -1;
    }

    static const char *empty_header[] = {NULL};
    switch (file_type) {
    case (REGULAR): {
        FILE *f = fopen(url_path, "r");
        if (f == NULL) {
            write_response(conn, 500, empty_header, "Could not read file");
            return -1;
        }

        struct stat f_stat;
        if (stat(url_path, &f_stat) == -1) {
            write_response(conn, 500, empty_header, "Could not get file stats");
            log_error("Could not get file stats: %s", strerror(errno));
            fclose(f);
            return -1;
        }
        off_t file_size = f_stat.st_size;
        // log_info("file_size: %zd", file_size);

        // FIXME: Handle error
        char *file_content = calloc(file_size + 1, sizeof(char));
        ssize_t bytes_read = fread(file_content, sizeof(char), file_size, f);
        fclose(f);

        const char *header[] = {"Content-Type: text/simple",
                                "Connection: close", NULL};
        if (write_response(conn, 200, header, file_content) == -1) {
            return -1;
        }
        break;
    }
    case (DIRECTORY): {
        char body_buffer[8192];
        if (generate_directory_listing_html(url_path, body_buffer,
                                            sizeof(body_buffer)) == -1) {
            return -1;
        }
        const char *header[] = {"Content-Type: text/html", "Connection: close",
                                NULL};
        if (write_response(conn, 200, header, body_buffer) == -1) {
            return -1;
        }
        break;
    }
    default:
        if (write_response(conn, 404, empty_header, NULL) == -1) {
            return -1;
        }
        break;
    }

    return 0;
}

// Handle the new connection (send/receive data)
// NOTE: For now we just want to send back exactly what we receive
int handle_connection(int conn_sockfd, http_parser *parser) {
    // FIXME: Handle error
    struct connection *conn = new_connection(conn_sockfd);

    parser->data = conn;

    if (read_request(parser, conn) < 0) {
        goto close_connection;
    }

    if (send_response(conn) < 0) {
        goto close_connection;
    }

close_connection:
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
            log_error("Failed to read file descriptor from socket: %s",
                      strerror(errno));
            // FIXME: It might mean that the parent process wasn't
            // successfully initialized. For now terminate the worker
            // when this happens.
            exit(EXIT_FAILURE); // FIXME
        } else {
            // log_info("Message received, connection socket fd: %d\n",
            //          conn_sockfd);
            if (handle_connection(conn_sockfd, parser) < 0) {
                log_error("Failed to handle connection\n");
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
        log_info("Worker process with id %d spawned", pid);
        close(ipc_sock_pair[1]);
        // Parent process
        w->pid = pid;
        w->ipc_sock = ipc_sock_pair[0];
        w->available = 1;
        w->up = 1;
    } else {
        log_error("Could not fork worker process: %s", strerror(errno));
    }

    return 0;
}

void spawn_workers(struct worker *workers) {
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (new_worker(&workers[i]) == -1) {
            // NOTE: Let's not think about retries for now
            log_warn("Failed to spawn worker with id %d", i);
        }
    }
}
