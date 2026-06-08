/*  shm_subscriber.c
 *
 *  Subscriber Process:
 *  - Opens the existing Shared Memory segment (created by publisher)
 *  - Opens the existing Semaphore (created by publisher)
 *  - Blocks on sem_wait() → wakes up only when publisher calls sem_post()
 *  - Reads data from Shared Memory after being notified
 *
 *  Synchronization Design:
 *
 *      Publisher                   Subscriber
 *      ─────────                   ──────────
 *      write data to SHM           sem_wait()  ← BLOCKS here (semaphore = 0)
 *      sem_post()  ─── signal ───▶ wakes up
 *                                  read SHM
 *                                  sem_wait()  ← BLOCKS again, waits for next update
 *
 *  Why semaphore and NOT busy-polling?
 *  - Busy-polling wastes CPU: while(1) { check SHM; }
 *  - sem_wait() puts the process to SLEEP in kernel → 0% CPU while waiting
 *  - Kernel wakes it up instantly when publisher calls sem_post()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>
#include <signal.h>

/* ── Must match publisher definitions ── */
#define SHM_NAME    "/shm_pubsub_demo"
#define SEM_NAME    "/sem_pubsub_demo"

typedef struct {
    int  msg_id;
    char payload[256];
} SharedData_t;

/* ── Graceful shutdown on Ctrl+C ── */
static volatile int keep_running = 1;
static void sig_handler(int signo) {
    (void)signo;
    keep_running = 0;
}

int main(void) {
    signal(SIGINT, sig_handler);

    /* ── 1. Open existing Shared Memory (publisher must be running first) ── */
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (shm_fd == -1) {
        perror("Subscriber: shm_open() failed — is publisher running?");
        exit(EXIT_FAILURE);
    }

    /* ── 2. Map SHM into this process (read-only — subscriber never writes) ── */
    SharedData_t *shm_ptr = (SharedData_t *)mmap(
        NULL,
        sizeof(SharedData_t),
        PROT_READ,
        MAP_SHARED,
        shm_fd,
        0
    );
    if (shm_ptr == MAP_FAILED) {
        perror("Subscriber: mmap() failed");
        exit(EXIT_FAILURE);
    }
    close(shm_fd); /* fd no longer needed after mmap */

    /* ── 3. Open existing Semaphore (created by publisher) ── */
    sem_t *sem = sem_open(SEM_NAME, 0); /* 0 = open existing, no O_CREAT */
    if (sem == SEM_FAILED) {
        perror("Subscriber: sem_open() failed — is publisher running?");
        exit(EXIT_FAILURE);
    }

    printf("Subscriber: Connected to Shared Memory and Semaphore.\n");
    printf("Subscriber: Waiting for publisher updates... (Ctrl+C to quit)\n\n");

    /* ── 4. Subscribe loop ── */
    while (keep_running) {
        /* Block here until publisher calls sem_post() */
        /* sem_wait() is interrupted by signals (SIGINT), so we check errno */
        if (sem_wait(sem) == -1) {
            if (errno == EINTR) {
                /* Interrupted by Ctrl+C signal → exit gracefully */
                break;
            }
            perror("Subscriber: sem_wait() failed");
            break;
        }

        /* ── Read from Shared Memory ── */
        /* Make a local copy to avoid reading partially-written data in future examples */
        int   received_id = shm_ptr->msg_id;
        char  received_payload[256];
        strncpy(received_payload, shm_ptr->payload, sizeof(received_payload));

        printf("Subscriber: Received update → [msg_id=%d] \"%s\"\n",
               received_id, received_payload);
    }

    /* ── 5. Cleanup ── */
    munmap(shm_ptr, sizeof(SharedData_t));
    sem_close(sem);
    /* NOTE: subscriber does NOT call shm_unlink() or sem_unlink()
     * Only the publisher (owner/creator) is responsible for cleanup */

    printf("\nSubscriber: Disconnected. Exiting.\n");
    return EXIT_SUCCESS;
}
