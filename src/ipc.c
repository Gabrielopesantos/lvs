#include "main.h"
#include <memory.h>
#include <netdb.h>
#include <stdio.h>

int send_fd(int socket, int fd) {
    // Prepare the message header
    struct msghdr msg;
    memset(&msg, 0, sizeof(struct msghdr));

    // Prepare the control message structure
    char cbuffer[CMSG_SPACE(sizeof(int))];
    memset(cbuffer, 0, sizeof(cbuffer));

    // Set the control message in the message header
    msg.msg_control = cbuffer;
    msg.msg_controllen = sizeof(cbuffer);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int)); // Length of control message structure
    cmsg->cmsg_type = SCM_RIGHTS;           // Specify the control message type
    cmsg->cmsg_level = SOL_SOCKET;          // Socket-level control message

    // Attach the file descriptor to the control message
    *((int *)CMSG_DATA(cmsg)) = fd;

    printf("About to send message with conn fd %d to socket fd %d\n", fd, socket);
    int ret = sendmsg(socket, &msg, 0);
    if (ret == -1) {
        perror("sendmsg");
        return -1;
    }

    return 0;
}

int receive_fd(int socket, int *fd) {
    // Prepare the parent message header for receiving
    struct msghdr msg;
    memset(&msg, 0, sizeof(struct msghdr));

    // Prepare the control message structure
    char recv_cbuffer[CMSG_SPACE(sizeof(int))];
    memset(recv_cbuffer, 0, sizeof(recv_cbuffer));

    // Set the control buffer in the receive message header
    msg.msg_control = recv_cbuffer;
    msg.msg_controllen = sizeof(recv_cbuffer);

    if (recvmsg(socket, &msg, 0) == -1) {
        perror("recvmsg"); // NOTE: Is this needed if we use strerror on the caller?
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL) {
        fprintf(stderr, "ERROR: No control message received\n");
        return -1;
    }

    *fd = *((int *)CMSG_DATA(cmsg));
    return 0;
}
