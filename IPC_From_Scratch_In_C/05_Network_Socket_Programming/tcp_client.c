/* Changes from Unix Domain Socket → Network Socket:
 *    AF_UNIX + sockaddr_un  →  AF_INET + sockaddr_in
 *    File path              →  Server IP + Port
 *
 *  Usage: ./tcp_client <server_ip>
 *  e.g. ./tcp_client 127.0.0.1       (same machine)
 *       ./tcp_client 192.168.1.100   (remote machine)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>      /* inet_pton: string IP → binary */

#define SERVER_PORT 9090
#define BUFFER_SIZE 128

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        fprintf(stderr, "  e.g: %s 127.0.0.1\n", argv[0]);
        fprintf(stderr, "  e.g: %s 192.168.1.100\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int ret;
    int data_socket;
    char buffer[BUFFER_SIZE];
    int i;
    int result;

    /* ── 1. Create TCP socket ── */
    data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (data_socket == -1) { perror("socket"); exit(EXIT_FAILURE); }

    /* ── 2. Construct server address (IP + Port instead of file path) ── */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_PORT);

    /* inet_pton: convert string "192.168.1.100" → binary IP in network byte order */
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

#if 0
    /* Unix Domain Socket used:
     *   addr.sun_family = AF_UNIX;
     *   strncpy(addr.sun_path, "/tmp/DemoSocket", ...);
     * Network Socket uses:
     *   addr.sin_family = AF_INET;
     *   addr.sin_addr   = 192.168.1.100 (binary);
     *   addr.sin_port   = 9090 (network byte order); */
#endif

    /* ── 3. Connect to server ── */
    ret = connect(data_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1) {
        fprintf(stderr, "Cannot connect to server %s:%d\n", argv[1], SERVER_PORT);
        perror("connect");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s:%d\n\n", argv[1], SERVER_PORT);

    /* ── 4. Same logic as client_1.c — send integers, 0 to get result ── */
    do {
        printf("Enter number (0 = get result): ");
        scanf("%d", &i);
        ret = write(data_socket, &i, sizeof(i));
        if (ret == -1) { perror("write"); exit(EXIT_FAILURE); }
        printf("Sent %d (%d bytes)\n", i, ret);
    } while (i);

    /* ── 5. Read result from server ── */
    ret = read(data_socket, buffer, BUFFER_SIZE);
    if (ret == -1) { perror("read"); exit(EXIT_FAILURE); }

    memcpy(&result, buffer, sizeof(result));
    printf("\nResult from server: %d\n", result);

    close(data_socket);
    return EXIT_SUCCESS;
}
