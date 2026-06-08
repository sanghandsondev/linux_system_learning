#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>

#define SOCKET_NAME "/tmp/DemoSocket"
#define BUFFER_SIZE 128

#define MAX_CLIENT_SUPPORTED    32

/*  This array will hold the file descriptors of the connected clients. 
    The server will monitor these file descriptors for any activity (e.g., incoming data) using the select() system call. 
    The server will also monitor the connection socket (master socket) for new connection requests from clients. 
    The server will use the select() system call to check for activity on all these file descriptors and handle them accordingly.
*/
int monitored_fd_set[MAX_CLIENT_SUPPORTED];

int client_result[MAX_CLIENT_SUPPORTED] = {0}; 

/* Remove all the FDs, if any, from the monitored_fd_set */
static void initialize_monitor_fd_set() {
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        monitored_fd_set[i] = -1; // -1 indicates that the slot is empty
    }
}

/* Add a client file descriptor to the monitored_fd_set */
static void add_to_monitor_fd_set(int fd) {
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] == -1) {
            monitored_fd_set[i] = fd;
            break;
        }
    }
}

/* Remove a client file descriptor from the monitored_fd_set */
static void remove_from_monitor_fd_set(int fd) {
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] == fd) {
            monitored_fd_set[i] = -1;
            break;
        }
    }
}

/* Clone all the FDs in the monitored_fd_set into fd_set */
static void refresh_fd_set(fd_set *fdset) {
    FD_ZERO(fdset);
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] != -1) {
            FD_SET(monitored_fd_set[i], fdset);
        }
    }
}

/* Get the numberical max value among all FDs which server is monitoring*/
static int get_max_fd() {
    int max_fd = -1;
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] > max_fd) {
            max_fd = monitored_fd_set[i];
        }
    }
    return max_fd;
}

int main(int argc, char *argv[]) {
    /* Initialize the monitored_fd_set to keep track of client file descriptors. */
    initialize_monitor_fd_set();

    struct sockaddr_un name;
#if 0
    struct sockaddr_un {
        sa_family_t sun_family;                     // Address family (AF_UNIX)
        char        sun_path[108];                  // Null-terminated pathname
    };
#endif
    int ret;
    int connection_socket;
    int client_socket;
    int data_socket;

    char buffer[BUFFER_SIZE];
    int data;
    int result;

    fd_set read_fds;
    int comm_socket_fd, i;

    /* In case the program exited inadvertently on the last run, remove the socket */
    unlink(SOCKET_NAME);

    connection_socket = socket(AF_UNIX, SOCK_STREAM, 0);        
    if (connection_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    printf("Master socket created.\n");

    memset(&name, 0, sizeof(struct sockaddr_un));
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);

    ret = bind(connection_socket, (const struct sockaddr *) &name,
               sizeof(struct sockaddr_un));
    if (ret == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    ret = listen(connection_socket, 20);
    if (ret == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    /* Add master socket to Monitored set of FDs*/
    add_to_monitor_fd_set(connection_socket);

    for (;;) {
        refresh_fd_set(&read_fds);

        /* Wait for activity on the monitored file descriptors. */
        /* select() system call will block until there is activity. Server will sleep here */
        int activity = select(get_max_fd() + 1, &read_fds, NULL, NULL, NULL);
        if (activity == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        /* Check if there is activity on the master socket (new connection request) */
        if (FD_ISSET(connection_socket, &read_fds)) {
            printf("New connection request received. Accept the connection..\n");
            struct sockaddr_un client_name;
            socklen_t client_name_len = sizeof(client_name);
            client_socket = accept(connection_socket, (struct sockaddr *) &client_name, &client_name_len);
            if (client_socket == -1) {
                perror("accept");
                continue;
            }

            printf("New client connected: %s\n", client_name.sun_path);
            add_to_monitor_fd_set(client_socket);
        } else {
            /* Check for activity on client sockets */
            for (i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
                comm_socket_fd = monitored_fd_set[i];
                if (comm_socket_fd != -1 && FD_ISSET(comm_socket_fd, &read_fds)) {
                    printf("Data received from client socket FD: %d\n", comm_socket_fd);
                    memset(buffer, 0, BUFFER_SIZE);

                    /* Read data from the client socket. Server is blocked here */
                    ret = read(comm_socket_fd, buffer, BUFFER_SIZE);
                    if (ret == -1) {
                        perror("read");
                        exit(EXIT_FAILURE);
                    }

                    memcpy(&data, buffer, sizeof(int));
                    if (data == 0) {
                        memset(buffer, 0, BUFFER_SIZE);
                        memcpy(buffer, &client_result[i], sizeof(int));

                        /* Send the result back to the client. */
                        ret = write(comm_socket_fd, buffer, BUFFER_SIZE);
                        if (ret == -1) {
                            perror("write");
                            exit(EXIT_FAILURE);
                        }
                        printf("Result sent back to client socket FD %d: %d\n", comm_socket_fd, client_result[i]);

                        close(comm_socket_fd);
                        client_result[i] = 0; // reset the result for this client
                        remove_from_monitor_fd_set(comm_socket_fd);
                        printf("Client socket FD %d disconnected.\n", comm_socket_fd);
                    } else {
                        client_result[i] += data;
                    }
                }
            }
        }

        /* Go to select() and block, wait for activity on the monitored file descriptors */
    }

    close(connection_socket);
    remove_from_monitor_fd_set(connection_socket);
    printf("Connection socket (master socket) closed.\n");

    unlink(SOCKET_NAME);
    exit(EXIT_SUCCESS);
}