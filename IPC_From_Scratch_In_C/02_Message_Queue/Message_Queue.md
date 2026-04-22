1. Message Queue
- A Message Queue is identified uniquely by the ID (which is a string). And it resides and is managed by the Kernel/OS.
- Process can create a new message QUEUE or can use an existing msgQUEUE which was created by another process.
    (There can be multiple message Queues created by several proccesses in the system.)
- Process A (Sender) with post the data to the message QUEUE, and the process B (receiver) reads the data from that.

Process A ----------- Message Queue (Kernel/OS) -----------> Process B

2. Message Queue Creation
- If mq_open() succeeds, it returns a file descriptor(a handle) to msgQ -> Using this handle
    mqd_t mq_open(const char* name, int oflag);
    mqd_t mq_open(const char* name, int oflag, mode_t mode, struct mq_attr *attr);

+ name: Name of msgQ, e.g., "/server-msg-q", "/mq/msgqueue" (unique)
+ oflag: operational flags
    O_RDONLY: Process can only read msgs from msgQ
    O_WRONLY: Process can only write msgs into the msgQ
    O_RDWR:   Process can write and read msgs to and from msgQ
    O_CREAT: The queue is created if not exist already
    O_EXCL:  mq_open() fails if process try to open an existing msgQ
+ mode: permissions set by the owning process on the queue, usually 0660
+ attr: specify various attributes of the msgQ being created.

struct mq_attr {
    long mq_flags;      // Flags: 0
    long mq_maxmsg;     // Max number of messages on queue
    long mq_msgsize;    // Max size (bytes) of a message
    long mq_curmsgs;    // Number of msgs currently in queue
};

Example: mqd_t msgq = mq_open("/server-msg-q", O_RDONLY | O_CREAT | O_EXCL, 0660, 0);
Example 2: mqt_t msgq;
           struct mq_attr attr;

           attr.mq_flags = 0;
           attr.mq_maxmsg = 10;
           attr.mq_msgsize = 4096;
           attr.mq_curmsgs = 0;

           msgq = mq_open("/server-msg-q", O_RDONLY | O_CREAT | O_EXCL, 0660, &attr);

3. Message Queue Closing
- OS indicates that by reference_count (number of processes which opening the msgQ from mq_open())
- So when reference_count = 0 -> Kernel/OS removes and destroy that kernel resource (msgQ).
    int mq_close(mqd_t msgQ);

4. Enqueue a Message
    int mq_send(mqd_t msgQ, char* msg_ptr, size_t msg_len, unsigned int msg_prio);

- mq_send() is for sending a message to the queue refered by the descriptor msgQ
+ msg_ptr: the message buffer
+ msg_len: the size of the message, which should be less than or equal to the message size for the queue.
+ msg_prio: the message priority, which is a non-negative (>= 0) number. Messages are placed in the queue in the decreasing order of message priority.

- If the queue is FULL, mq_send() blocks till there is space on the queue, unless the O_NONBLOCK flag is enabled for the message queue
    --> In which case, mq_send() returns immediately with errno set to EAGAIN.

5. Dequeue a Message
    int mq_receive(mqd_t msgQ, char* msg_ptr, size_t msg_len, unsigned int *msg_prio);

+ msg_ptr: point to empty messasge buffer
+ msg_len: size of the buffer in bytes
- The oldest msg of the highest priority is deleted from the queue and passed to the process in the buffer pointed by msg_ptr
- If the pointer msg_prio is not null, the priority of the received msg = *msg_prio (value of pointer)
- The default behavior of mq_receive() is to block if there is no messasge in the queue.
    However, if the O_NONBLOCK flag is enabled for the queue, and the queue is empty. 
    -> mq_receive() returns immediately with errno set to EAGAIN.

- On success, mq_receive() returns the number of bytes receveid in the buffer pointed by msg_ptr (maybe = msg_len)

6. Using as an IPC
- A message queue IPC mechanism usually supports N:1 communication paradiagm, meaning can be N senders but 1 receiver per message Queue.
    Sender process ----
    Sender process -----|-----> Message Queue (Kernel/OS) --------> Receiver process
    Sender process ----

- However, receiving process can dequeue msgs from different messsage Queues at the same time --> MULTIPLEXING using select()

- NOTE: Khi một Message Queue đang được một process sử dụng mq_receive() block thì các process khác sẽ ko chen chân vào được
      --> Vì vậy có thể nói Process muốn nhận msg từ cơ chế Message Queue này thì phải tự tạo ra một MsgQ và block mq_receive()