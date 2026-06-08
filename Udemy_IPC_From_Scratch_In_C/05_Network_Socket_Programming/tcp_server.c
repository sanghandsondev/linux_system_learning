/*  tcp_server.c — Network Socket version of server_2_multiplexing_select.c
 *
 *  Changes from Unix Domain Socket → Network Socket:
 *  ┌─────────────────────┬─────────────────────────────────┐
 *  │ Unix Domain Socket  │ Network (TCP) Socket             │
 *  ├─────────────────────┼─────────────────────────────────┤
 *  │ AF_UNIX             │ AF_INET                          │
 *  │ sockaddr_un         │ sockaddr_in                      │
 *  │ sun_path (file)     │ sin_addr + sin_port (IP:Port)    │
 *  │ unlink() cleanup    │ SO_REUSEADDR (port reuse)        │
 *  └─────────────────────┴─────────────────────────────────┘
 *
 *  Same logic: client sends integers, server sums them, sends result on 0.
 *
 *  Usage: ./tcp_server
 *         Listens on 0.0.0.0:9090 (all interfaces)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>     /* sockaddr_in, htons, INADDR_ANY */
#include <arpa/inet.h>      /* inet_ntoa */
#include <sys/select.h>

#define SERVER_PORT          9090
#define BUFFER_SIZE          128
#define MAX_CLIENT_SUPPORTED 32

int monitored_fd_set[MAX_CLIENT_SUPPORTED];
int client_result[MAX_CLIENT_SUPPORTED] = {0};

static void initialize_monitor_fd_set() {
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++)
        monitored_fd_set[i] = -1;
}

static void add_to_monitor_fd_set(int fd) {
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] == -1) { monitored_fd_set[i] = fd; break; }
    }
}

static void remove_from_monitor_fd_set(int fd) {
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] == fd) { monitored_fd_set[i] = -1; break; }
    }
}

static void refresh_fd_set(fd_set *fdset) {
    FD_ZERO(fdset);
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] != -1) FD_SET(monitored_fd_set[i], fdset);
    }
}

static int get_max_fd() {
    int max = -1;
    for (int i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
        if (monitored_fd_set[i] > max) max = monitored_fd_set[i];
    }
    return max;
}

int main(void) {
    initialize_monitor_fd_set();

    int ret;
    int connection_socket;
    int client_socket;
    char buffer[BUFFER_SIZE];
    int data;
    fd_set read_fds;
    int comm_socket_fd, i;

    /* ── 1. Create TCP socket (AF_INET instead of AF_UNIX) ── */
    connection_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connection_socket == -1) { perror("socket"); exit(EXIT_FAILURE); }

    /* SO_REUSEADDR: allow rebind immediately after server restart
     * (replaces unlink() which was used for Unix Domain Socket) */
    int opt = 1;
    setsockopt(connection_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    printf("Master socket created.\n");

    /* ── 2. Bind to IP:Port (instead of file path) ── */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;          /* IPv4 */
    server_addr.sin_addr.s_addr = INADDR_ANY;       /* 0.0.0.0 — listen on all interfaces */
    server_addr.sin_port        = htons(SERVER_PORT);/* host-to-network byte order */

#if 0
    /* Unix Domain Socket used:
     *   struct sockaddr_un { AF_UNIX, sun_path="/tmp/DemoSocket" }
     * Network Socket uses:
     *   struct sockaddr_in { AF_INET, sin_addr=0.0.0.0, sin_port=9090 } */
#endif

    ret = bind(connection_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1) { perror("bind"); exit(EXIT_FAILURE); }

    ret = listen(connection_socket, 20);
    if (ret == -1) { perror("listen"); exit(EXIT_FAILURE); }

    printf("Server listening on 0.0.0.0:%d\n", SERVER_PORT);

    add_to_monitor_fd_set(connection_socket);

    /* ── 3. select() loop — identical to Unix Domain Socket version ── */
    for (;;) {
        refresh_fd_set(&read_fds);

        int activity = select(get_max_fd() + 1, &read_fds, NULL, NULL, NULL);
        if (activity == -1) { perror("select"); exit(EXIT_FAILURE); }

        /* New connection? */
        if (FD_ISSET(connection_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            client_socket = accept(connection_socket, (struct sockaddr *)&client_addr, &client_len);
            if (client_socket == -1) { perror("accept"); continue; }

            /* inet_ntoa + ntohs: convert binary IP/Port to readable string */
            printf("New client connected: %s:%d (FD=%d)\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port),
                   client_socket);
            add_to_monitor_fd_set(client_socket);
        } else {
            /* Data from existing client — same logic as Unix Domain Socket */
            for (i = 0; i < MAX_CLIENT_SUPPORTED; i++) {
                comm_socket_fd = monitored_fd_set[i];
                if (comm_socket_fd != -1 && FD_ISSET(comm_socket_fd, &read_fds)) {
                    memset(buffer, 0, BUFFER_SIZE);
                    ret = read(comm_socket_fd, buffer, BUFFER_SIZE);
                    if (ret <= 0) {
                        printf("Client FD %d disconnected.\n", comm_socket_fd);
                        close(comm_socket_fd);
                        client_result[i] = 0;
                        remove_from_monitor_fd_set(comm_socket_fd);
                        continue;
                    }

                    memcpy(&data, buffer, sizeof(int));
                    if (data == 0) {
                        /* Send accumulated result back */
                        memset(buffer, 0, BUFFER_SIZE);
                        memcpy(buffer, &client_result[i], sizeof(int));
                        ret = write(comm_socket_fd, buffer, BUFFER_SIZE);
                        if (ret == -1) { perror("write"); exit(EXIT_FAILURE); }

                        printf("Result sent to client FD %d: %d\n", comm_socket_fd, client_result[i]);
                        close(comm_socket_fd);
                        client_result[i] = 0;
                        remove_from_monitor_fd_set(comm_socket_fd);
                        printf("Client FD %d disconnected.\n", comm_socket_fd);
                    } else {
                        client_result[i] += data;
                        printf("Client FD %d sent %d, running sum = %d\n",
                               comm_socket_fd, data, client_result[i]);
                    }
                }
            }
        }
    }

    close(connection_socket);
    return EXIT_SUCCESS;
}
