1. Signals
- is a system message sent from one process to another -> interrupt
- not usually used to transfer data but instead used to remotely command the partnered process
- small msg (1 or 2 bytes)

App1 -------Signal (kill)-------> App2
or
OS/kernel ----- signal -------> App
or
App ----- raise(SIG...), abort() ----> App itself

2. Signal Catching and Signal Handler Routine
(1) Default: Process execute the defaut action of the signal (e.g, when process receive SIGTERM signal -> process is terminated by default)
(2) Customized: Process do its customized processing on receiving the signal by executing the "signal handler routine"
    E.g: Process might want to print the Goodbye msg before it dies out when it receives the SIGTERM signal
(3) Ignore: Process ignore the signal

- A signal handler routine is special function which is invoked then the process receives a signal.
    (The current thread will interrupt the current function --> signal handler)
- Signal handler routine are executed at the highest priority, preempting the normal execution flow of the process.

3. Linux Well Known Signals
SIGINT              - interrupt (i.e., Ctrl C)
SIGUSR1 and SIGUSR2 - user defined signals
SIGKILL             - sent to process from kernel when kill -9 is invoked on pid. This signal cannot be caught by the process
SIGABRT             - raised by abort() by the process itself. Cannot be blocked. The process is terminated.
SIGTERM             - raised when kill is invoked. Can be caught by the process to execute user defined action.
SIGSEGV             - Segmentation fault, raised by the kernel to the process when "illegal memory" is referencted
SIGCHILD            - Whenever a child terminates, this signal is sent to the parent
    Upon receiving this signal, parent should execute wait() system call to read child status.
    Need to understand fork() to understand this signal.