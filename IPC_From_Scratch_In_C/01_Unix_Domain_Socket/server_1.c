#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>

// PROBLEM of server_1.c:
// While Server is servicing one client, other clients will be blocked and cannot connect to the server. 
// This is because the server is handling the client in a blocking manner. The server will not be able to accept new connections until it finishes handling the current client. 
// This can lead to poor performance and scalability issues, especially if the server takes a long time to process each client's request.
// --> Re-design the to server multiple clients concurrently at the same time using the concept of MULTIPLEXING (select() or poll())

#define SOCKET_NAME "/tmp/DemoSocket"
#define BUFFER_SIZE 128

int main(int argc, char *argv[]) {
    struct sockaddr_un name;
#if 0
    struct sockaddr_un {
        sa_family_t sun_family;                     // Address family (AF_UNIX)
        char        sun_path[108];                  // Null-terminated pathname
    };
#endif
    int ret;
    int connection_socket;
    int data_socket;
    char buffer[BUFFER_SIZE];
    int result;
    int data;

    /* In case the program exited inadvertently on the last run, remove the socket */
    unlink(SOCKET_NAME);

    /* Create MASTER local socket. */
    /* SOCK_STREAM provides sequenced, reliable, two-way, connection-based byte streams. An out-of-band data transmission mechanism may be supported.*/ 
    /* SOCK_DGRAM provides datagrams, which are connectionless, unreliable messages of a fixed maximum length. */
    connection_socket = socket(AF_UNIX, SOCK_STREAM, 0);        
    if (connection_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Bind socket to socket name. */
    memset(&name, 0, sizeof(struct sockaddr_un));
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);

    /* Bind the socket to the name. */
    /* Purpose of bind() system call is that it associates a socket with a specific address (in this case, a file system pathname). 
       It is telling the OS that if sender process sends the data to the socket name, it should be delivered to this socket. */
    ret = bind(connection_socket, (const struct sockaddr *) &name,
               sizeof(struct sockaddr_un));
    if (ret == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    /* Prepare for accepting connections. 
       The backlog parameter defines the maximum length to which the queue of pending connections for the socket may grow. */
    ret = listen(connection_socket, 20);
    if (ret == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    /* Main loop: Wait for clients. Handle each connection in turn. */
    for (;;) {
        /* Wait for a client connection. This accept() will block until a connection is made. */
        printf("Server is listening...\n");
        data_socket = accept(connection_socket, NULL, NULL);

        if (data_socket == -1) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        result = 0;
        printf("One Client connected.\n");

        for(;;) {
            /* Prepare the buffer to recv the data*/
            memset(buffer, 0, BUFFER_SIZE);

            /* Wait for data. This read() will block until data is available. 
               The read() system call attempts to read up to count bytes from file descriptor fd into the buffer starting at buf. */
            ret = read(data_socket, buffer, BUFFER_SIZE);
            if (ret == -1) {
                perror("read");
                exit(EXIT_FAILURE);
            }

            memcpy(&data, buffer, sizeof(int));
            if (data == 0) break;
            result += data;
        }

        /* Send the result back to the client.*/
        memset(buffer, 0, BUFFER_SIZE);
        memcpy(buffer, &result, sizeof(int));

        /* The write() system call writes up to count bytes from the buffer starting at buf to the file referred to by the file descriptor fd. */
        ret = write(data_socket, buffer, BUFFER_SIZE);
        if (ret == -1) {
            perror("write");
            exit(EXIT_FAILURE);
        }

        printf("Result sent back to client: %d\n", result);

        /* Close the data socket. */
        close(data_socket);
    }

    /* Unreachable code, but if we ever exit the loop, clean up. */
    /* Close the connection socket (master socket) */
    close(connection_socket);

    /* Server should release resources before getting terminated. */
    unlink(SOCKET_NAME);

    exit(EXIT_SUCCESS);
}