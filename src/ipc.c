#include "main.h"
#include <memory.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>

int send_fd(int socket, int fd) {
    struct iovec iov[1];
    char dummy;
    iov[0].iov_base = &dummy;
    iov[0].iov_len = 1;

    struct msghdr msg = {0};
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    char buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    *((int *)CMSG_DATA(cmsg)) = fd;

    return sendmsg(socket, &msg, 0);
}

int receive_fd(int socket, int *fd) {
    // NOTE: iovec doesn't seem to be needed in receiver
    struct iovec iov[1];
    char dummy;
    iov[0].iov_base = &dummy;
    iov[0].iov_len = 1;

    struct msghdr msg = {0};
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    char buf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    if (recvmsg(socket, &msg, 0) < 0) {
        perror("recvmsg");
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        return -1;
    }

    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        return -1;
    }

    *fd = *((int *)CMSG_DATA(cmsg));
    return 0;
}
