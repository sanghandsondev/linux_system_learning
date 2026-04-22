#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <errno.h>
#include <unistd.h>

#define MAX_MSG_SIZE 256
#define MSG_BUFFER_SIZE (MAX_MSG_SIZE + 10)

int main(int argc, char *argv[]) {
    char buffer[MSG_BUFFER_SIZE];
    mqd_t recvr_msgq_fd = (mqd_t)-1;

    memset(buffer, 0, MSG_BUFFER_SIZE);

    /* Block until receiver creates the queue.
     * O_WRONLY without O_CREAT: fails with ENOENT if queue doesn't exist yet. */
    printf("Sender: waiting for receiver to create the queue...\n");
    while ((recvr_msgq_fd = mq_open("/my_msg_queue", O_WRONLY)) == (mqd_t)-1) {
        if (errno == ENOENT) {
            /* Queue not yet created by receiver — keep retrying */
            sleep(1);
        } else {
            printf("Sender: mq_open() failed. Errno = %d\n", errno);
            exit(EXIT_FAILURE);
        }
    }

    printf("Message queue opened successfully. Ready to send messages.\n");

    while (1) {
        printf("Enter the message to send: \n");
        scanf("%s", buffer);

        if (mq_send(recvr_msgq_fd, buffer, strlen(buffer) + 1, 0) == -1) {
            printf("Client: mq_send() failed. Errno = %d\n", errno);
            exit(EXIT_FAILURE);
        }
    }

    mq_close(recvr_msgq_fd);
    printf("Message sent successfully. Exiting.\n");
    exit(EXIT_SUCCESS);
}