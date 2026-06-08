/*  shm_publisher.c
 *
 *  Publisher Process:
 *  - Creates and owns the Shared Memory segment
 *  - Creates the semaphore (initialized to 0 → subscribers will block on sem_wait)
 *  - Writes data into Shared Memory
 *  - After each write → calls sem_post() to notify ALL subscribers
 *
 *  Synchronization Design:
 *
 *      Publisher                   Subscriber(s)
 *      ─────────                   ─────────────
 *      write data to SHM           sem_wait()  ← block here until notified
 *      sem_post()  ─── signal ───▶ wake up → read SHM
 *
 *  NOTE: For multiple subscribers, publisher calls sem_post() once per subscriber.
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

/* ── Shared resource names (agreed upon between publisher & subscribers) ── */
#define SHM_NAME        "/shm_pubsub_demo"
#define SEM_NAME        "/sem_pubsub_demo"

/* Number of subscribers this publisher will notify per update */
#define NUM_SUBSCRIBERS 1

/* Layout of the shared memory region */
typedef struct {
    int  msg_id;                  /* monotonically increasing message counter */
    char payload[256];            /* actual data */
} SharedData_t;

int main(void) {
    /* ── 1. Create Shared Memory ── */
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_TRUNC, 0660);
    if (shm_fd == -1) {
        perror("Publisher: shm_open() failed");
        exit(EXIT_FAILURE);
    }

    /* ── 2. Set the size of the SHM segment ── */
    if (ftruncate(shm_fd, sizeof(SharedData_t)) == -1) {
        perror("Publisher: ftruncate() failed");
        exit(EXIT_FAILURE);
    }

    /* ── 3. Map the SHM segment into this process's address space ── */
    SharedData_t *shm_ptr = (SharedData_t *)mmap(
        NULL,
        sizeof(SharedData_t),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        shm_fd,
        0
    );
    if (shm_ptr == MAP_FAILED) {
        perror("Publisher: mmap() failed");
        exit(EXIT_FAILURE);
    }
    memset(shm_ptr, 0, sizeof(SharedData_t));
    close(shm_fd); /* fd no longer needed after mmap */

    /* ── 4. Create Semaphore (value = 0 → subscribers block until notified) ── */
    /* O_CREAT | O_EXCL ensures publisher is always the creator.
     * Clean up any stale semaphore from a previous run first. */
    sem_unlink(SEM_NAME);
    sem_t *sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0660, 0);
    if (sem == SEM_FAILED) {
        perror("Publisher: sem_open() failed");
        exit(EXIT_FAILURE);
    }

    printf("Publisher: Shared Memory and Semaphore created.\n");
    printf("Publisher: Start subscriber(s) in another terminal, then press ENTER to send updates.\n\n");

    /* ── 5. Publish loop ── */
    int msg_id = 0;
    char input[256];

    while (1) {
        printf("Enter message to publish (or 'q' to quit): ");
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        /* Remove newline */
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "q") == 0) break;

        /* ── Write to Shared Memory ── */
        msg_id++;
        shm_ptr->msg_id = msg_id;
        strncpy(shm_ptr->payload, input, sizeof(shm_ptr->payload) - 1);
        shm_ptr->payload[sizeof(shm_ptr->payload) - 1] = '\0';

        printf("Publisher: Wrote to SHM → [msg_id=%d] \"%s\"\n", msg_id, input);

        /* ── Notify subscribers via semaphore ── 
         * Call sem_post() once per subscriber so each one wakes up exactly once. */
        for (int i = 0; i < NUM_SUBSCRIBERS; i++) {
            sem_post(sem);
        }
        printf("Publisher: Notified %d subscriber(s).\n\n", NUM_SUBSCRIBERS);
    }

    /* ── 6. Cleanup ── */
    munmap(shm_ptr, sizeof(SharedData_t));
    shm_unlink(SHM_NAME);
    sem_close(sem);
    sem_unlink(SEM_NAME);

    printf("Publisher: Cleaned up. Exiting.\n");
    return EXIT_SUCCESS;
}
