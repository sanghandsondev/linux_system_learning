1. Shared Memory
Process A (Sender) --- write() ----> DATA (Shared Memory - RAM) ------ read() ----> Process B (Receiver)

Memory Mappings -> Shared Memory -> Using External Data Source as Shared Memory

- Virtual Pages of both the Processes maps to same physical pages loaded in RAM. 
    P1's Virtual Memory ----- MMU -----> RAM (Physical Memory) <------ MMU -------- P2's Virtual Memory

- Physical Pages in turn are read/written to external memory.
- Any modification made by P1 in its shared VirtualMemory, shall be seen by P2
--> Used Widely for IPC (not copying data --> fastest IPC)

2. Walk Steps
- Initialize the Shared Memory segment -> shm_open()
- Define the size of the SHM segment -> ftruncate()
- Map the Shared Memory segment to Data Source -> mmap()
- Use the Shared Memory -> read() / write()
- Destroy the mapping between process and Shared Memory segment -> munmap()
- Destroy shared memory segment - shm_unlink()

3. Design Constaints for using Shared Memory as IPC
- Shared Memory approach of IPC is used in a scenario where:
+ Exactly one processes is responsibile to update the shared memory (publisher process)
+ Rest of the processes only read the shared memory (Subscriber processes)
+ The frequency of updatting the shared memory by publisher process should not be very high
    For example, publisher process update the shared memory when user configure something on the software

- Shared Memory doesn't have built-in synch mechanism.
- So if multiple processes attempts to update the shared memory at the same time, then it leads to write-write conflict
-> Data race / Race condition
-> We need to handle this situation using Semaphore (Synchronization like Mutex but for multiple processes)

- When publisher process update the shared memory

        Process B1 <---- Notify update ---- Process A ---- Notify update ----> Process  B2
       (Subscriber)                        (Publisher)                        (Subscriber)
            |                                  |                                    |
          read()                            write()                               read()
            |                                  |                                    |
             ------------------ [DATA] Shared Memory - RAM -------------------------

+ Việc notify subscriber cũng sẽ sử dụng 'Semaphore'

NOTE: lưu ý khi biên dịch: gcc -g example.c -lrt -lpthread -o example