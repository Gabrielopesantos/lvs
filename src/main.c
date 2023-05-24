#include "main.h"
#include "worker.h"
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
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
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
    fprintf(stdout, "Server listening for connections...\n");

    struct sockaddr_in client_addr;
    socklen_t client_len;
    int newsockfd;
    while (1) {
        // Accept new connection
        client_len = sizeof(client_addr);

        newsockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (newsockfd == -1) {
            perror("accept");
            continue;
        }

        fprintf(stdout, "New client connection accepted.\n");
        //
        // Prepare the message header
        struct msghdr message;
        memset(&message, 0, sizeof(struct msghdr));

        // Prepare the control message structure
        char control_buffer[CMSG_SPACE(sizeof(int))]; // Allocate space for one file descriptor
        memset(control_buffer, 0, sizeof(control_buffer));

        // Set the control message in the message header
        message.msg_control = control_buffer;
        message.msg_controllen = sizeof(control_buffer);

        struct cmsghdr *control_message = CMSG_FIRSTHDR(&message);
        control_message->cmsg_len = CMSG_LEN(sizeof(int)); // Length of control message structure
        control_message->cmsg_type = SCM_RIGHTS;           // Specify the control message type
        control_message->cmsg_level = SOL_SOCKET;          // Socket-level control message

        // Attach the file descriptor to the control message
        int *fd_ptr = (int *)CMSG_DATA(control_message);
        fd_ptr[0] = newsockfd;

        printf("About to send message with conn fd %d to socket fd %d\n", newsockfd,
               workers[0].ipc_sock);
        if (sendmsg(workers[0].ipc_sock, &message, 0) == -1) {
            perror("send");
        }
        printf("Message sent!\n");

        // Handle the new connection (send/receive data)
        // NOTE: For now we just want to send back exactly what we receive
        // char msg_buf[BUFFER_SIZE];
        // ssize_t msg_size;
        // msg_size = recv(newsockfd, &msg_buf, BUFFER_SIZE, 0);
        // if (msg_size == -1) {
        //     perror("recv");
        //     close(newsockfd);
        //     continue;
        // }
        //
        // fprintf(stdout, "Received the message '%s' with size %zd\n", msg_buf, msg_size);
        //
        // // Write back
        // if (send(newsockfd, &msg_buf, msg_size, 0) == -1) {
        //     perror("send");
        //     close(newsockfd);
        //     continue;
        // }
        //
        // fprintf(stdout, "Message sent back to the client.\n");

        // close(newsockfd); // NOTE: I don't close this socket, right?
    }

    // Close the socket (Isn't closed not available anymore in sockets.h?)
    close(sockfd);

    exit(0);
}
