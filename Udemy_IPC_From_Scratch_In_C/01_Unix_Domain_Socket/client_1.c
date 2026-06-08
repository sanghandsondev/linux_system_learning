#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_NAME "/tmp/DemoSocket"
#define BUFFER_SIZE 128

int main(int argc, char *argv[]) {
    struct sockaddr_un addr;
    int ret;
    int data_socket;
    char buffer[BUFFER_SIZE];
    int i;
    int result;

    /* Create local data socket. */
    data_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (data_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Construct server address, and make the connection. */
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_NAME, sizeof(addr.sun_path) - 1);

    ret = connect(data_socket, (const struct sockaddr *) &addr,
                  sizeof(struct sockaddr_un));
    if (ret == -1) {
        fprintf(stderr, "The server is down.\n");
        exit(EXIT_FAILURE);
    }

    /* Send arguments to server. */
    do {
        printf("Enter number to send to server: \n");
        scanf ("%d", &i);
        ret = write(data_socket, &i, sizeof(i));
        if (ret == -1) {
            perror("write");
            exit(EXIT_FAILURE);
        }
        printf("No of bytes sent = %d, data sent = %d\n", ret, i);
    } while (i);

    /* Request result. read() system call will block until data is available. */
    ret = read(data_socket, &buffer, BUFFER_SIZE);
    if (ret == -1) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    memcpy(&result, buffer, sizeof(result));
    printf("Result received from server: %d\n", result);

    /* Close the data socket. */
    close(data_socket);

    exit(EXIT_SUCCESS);
}