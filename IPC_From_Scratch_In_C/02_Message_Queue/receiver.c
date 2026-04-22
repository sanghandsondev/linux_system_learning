#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#define MAX_MESSAGES 10
#define MAX_MSG_SIZE 256
#define MSG_BUFFER_SIZE (MAX_MSG_SIZE + 10)
#define QUEUE_PERMISSIONS 0660

int main(int argc, char *argv[]) {
    char buffer[MSG_BUFFER_SIZE];
    fd_set read_fds;
    mqd_t msgq_fd = 0;

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    /* O_EXCL ensures receiver is always the creator.
     * If queue already exists (e.g. stale from a previous run), clean it up first. */
    mq_unlink("/my_msg_queue");

    if ((msgq_fd = mq_open("/my_msg_queue", O_RDONLY | O_CREAT | O_EXCL, QUEUE_PERMISSIONS, &attr)) == -1) {
        printf("Receiver: mq_open() failed. Errno = %d\n", errno);
        exit(EXIT_FAILURE);
    }
    printf("Receiver: queue created successfully.\n");

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(msgq_fd, &read_fds);

        printf("Waiting for messages...\n");
        int activity = select(msgq_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(msgq_fd, &read_fds)) {
            memset(buffer, 0, MSG_BUFFER_SIZE);
            ssize_t bytes_read = mq_receive(msgq_fd, buffer, MSG_BUFFER_SIZE, NULL);
            if (bytes_read >= 0) {
                printf("Received message from Queue: %s\n", buffer);
            } else {
                perror("mq_receive");
                exit(EXIT_FAILURE);
            }
        }
    }

    mq_close(msgq_fd);
    mq_unlink("/my_msg_queue");
    exit(EXIT_SUCCESS);
}