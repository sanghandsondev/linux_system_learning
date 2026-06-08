#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>

/*  signal_handler.c
 *
 *  Demonstrates signal handling + ways a process sends signals:
 *    kill(pid, sig)   — send to another process
 *    raise(sig)       — send signal "sig" to SELF
 *    abort()          — send SIGABRT to self
 *    alarm(n)         — kernel sends SIGALRM after n seconds
 *
 *  Normal flow is INTERRUPTED when a signal arrives:
 *    main() ... work ... <<<SIGNAL>>> ... signal_handler() ... resume work
 *
 *  Test from another terminal:
 *    kill -USR1 <pid>    
 *    kill -USR2 <pid>
 *    kill -TERM <pid>    
 *    kill -9 <pid>  (SIGKILL — uncatchable)
 */

/* ── Signal handler state ── */
static volatile sig_atomic_t usr1_count = 0;  /* sig_atomic_t: atomic read/write in handler */
static volatile sig_atomic_t usr2_count = 0;
static volatile sig_atomic_t keep_running = 1;

/* ─────────────────────────────────────────────────────
 *  Signal handler rules:
 *  - Use write() not printf() — printf() is NOT async-signal-safe
 *  - Don't call malloc/free inside handler
 *  - Only set flags, let main() do the real work
 * ───────────────────────────────────────────────────── */

/* Handler for SIGINT (Ctrl+C) */
static void handle_SIGINT(int signo) {
    write(STDOUT_FILENO, "\n[SIGINT] Ctrl+C caught — process NOT dying. Use SIGTERM or SIGKILL.\n", 70);
}

/* Handler for SIGTERM (kill <pid>) */
static void handle_SIGTERM(int signo) {
    write(STDOUT_FILENO, "\n[SIGTERM] Graceful shutdown — setting keep_running=0.\n", 56);
    keep_running = 0;
}

/* Handler for SIGUSR1 — meaning is application-defined */
static void handle_SIGUSR1(int signo) {
    usr1_count++;
    write(STDOUT_FILENO, "[SIGUSR1] Received — e.g. reload config, start logging.\n", 57);
}

/* Handler for SIGUSR2 — meaning is application-defined */
static void handle_SIGUSR2(int signo) {
    usr2_count++;
    write(STDOUT_FILENO, "[SIGUSR2] Received — e.g. toggle debug mode.\n", 45);
}

/* Handler for SIGCHLD — kernel sends this when a child terminates */
static void handle_SIGCHLD(int signo) {
    write(STDOUT_FILENO, "[SIGCHLD] Child terminated — calling waitpid() to reap zombie.\n", 64);
    int status;
    pid_t pid;
    /* WNOHANG: non-blocking reap — loop handles multiple children at once */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            char msg[64];
            int n = snprintf(msg, sizeof(msg), "[SIGCHLD] Child PID=%d exited code=%d\n",
                             pid, WEXITSTATUS(status));
            write(STDOUT_FILENO, msg, n);
        }
    }
}

/* Handler for SIGSEGV — illegal memory access */
static void handle_SIGSEGV(int signo) {
    write(STDOUT_FILENO, "\n[SIGSEGV] Segfault! Log crash info here, then re-raise for core dump.\n", 72);
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV); /* restore default → kernel generates core dump */
}

/* Handler for SIGALRM — fired by alarm() timer */
static void handle_SIGALRM(int signo) {
    write(STDOUT_FILENO, "[SIGALRM] Timer expired.\n", 25);
}

/* ── Register all signal handlers using sigaction() ──
 * Preferred over signal() — portable, full control over SA_RESTART, masking, etc. */
static void register_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_RESTART; /* interrupted syscalls (read/select) auto-restart */
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = handle_SIGINT;   sigaction(SIGINT,  &sa, NULL);
    sa.sa_handler = handle_SIGTERM;  sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = handle_SIGUSR1;  sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = handle_SIGUSR2;  sigaction(SIGUSR2, &sa, NULL);
    sa.sa_handler = handle_SIGSEGV;  sigaction(SIGSEGV, &sa, NULL);
    sa.sa_handler = handle_SIGALRM;  sigaction(SIGALRM, &sa, NULL);

    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; /* NOCLDSTOP: only notify on child exit, not stop */
    sa.sa_handler = handle_SIGCHLD;  sigaction(SIGCHLD, &sa, NULL);

    /* SIGKILL and SIGSTOP: cannot be caught/blocked — kernel-only */
}

/* ── Demonstrate forking a child (triggers SIGCHLD) ── */
static void demo_fork_child(void) {
    printf("[main] Forking a child process...\n");
    pid_t pid = fork();

    if (pid == 0) {
        printf("[child PID=%d] Working for 1 second...\n", getpid());
        sleep(1);
        printf("[child PID=%d] Exiting with code 42.\n", getpid());
        exit(42);
    } else if (pid > 0) {
        printf("[main] Child PID=%d spawned. Parent continues.\n", pid);
    } else {
        perror("fork");
    }
}

/* ── Demonstrate raise() — process sends signal to ITSELF ── */
static void demo_raise(void) {
    printf("\n[main] --- raise() demo ---\n");

    /* raise(sig) == kill(getpid(), sig) — sends signal to self */
    printf("[main] Calling raise(SIGUSR1) — sending SIGUSR1 to self...\n");
    raise(SIGUSR1);  /* handler runs immediately, then returns here */

    printf("[main] raise(SIGUSR1) returned — execution resumed after handler.\n");

    printf("[main] Calling raise(SIGUSR2)...\n");
    raise(SIGUSR2);
    printf("[main] raise(SIGUSR2) returned.\n");

    /* alarm(n): kernel sends SIGALRM to self after n seconds — async version of raise */
    printf("[main] alarm(3) set — SIGALRM will fire in 3 seconds.\n");
    alarm(3);

    /* abort() sends SIGABRT to self — used to indicate fatal internal error
     * Uncomment to test (will terminate the process):
     * printf("[main] Calling abort() — sends SIGABRT to self...\n");
     * abort();
     */
}

int main(void) {
    register_signal_handlers();

    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║           Signal Handler Demo Process            ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  PID = %-5d                                     ║\n", getpid());
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  From another terminal:                          ║\n");
    printf("║  kill -USR1 %-5d                                ║\n", getpid());
    printf("║  kill -USR2 %-5d                                ║\n", getpid());
    printf("║  kill      %-5d  (SIGTERM — graceful exit)      ║\n", getpid());
    printf("║  kill -9   %-5d  (SIGKILL — uncatchable)        ║\n", getpid());
    printf("╚══════════════════════════════════════════════════╝\n\n");

    /* Demo: process sends signals to itself using raise() */
    demo_raise();

    /* Demo: fork child → triggers SIGCHLD when child exits */
    demo_fork_child();

    /* Main loop */
    int iteration = 0;
    while (keep_running) {
        iteration++;
        printf("[main] Iteration %d | USR1=%d | USR2=%d | sleeping 10s...\n",
               iteration, usr1_count, usr2_count);
        sleep(10);
    }

    printf("\n[main] Shutdown complete. USR1=%d USR2=%d. Goodbye!\n",
           usr1_count, usr2_count);
    return EXIT_SUCCESS;
}
