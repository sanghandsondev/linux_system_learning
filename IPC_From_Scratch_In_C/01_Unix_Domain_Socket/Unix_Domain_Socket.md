1. Sockets
Unix/Linux like OS provide Socket interface to carry out communication between various types of entities.
The Socket Interface are a bunch of Socket programming related APIs
We shall be using these APIs to implement the Sockets of Various types

2. Two types:
- Unix Domain Sockets (UDS): IPC between processes running on the SAME system (machine) -> FOCUS IN 
- Network Sockets (TCP/UDP): Communication between processes running on different physical machines over the network.

3. Socket Message Types
Flow:
     Client Process ------------ CIR -------------> Server Process
                    <--- Established connection --> 
                    ------------ SRM -------------> 
                    <--------- Response -----------

Categorized 2 message types:
- Connection initiation request (CIR) messages
- Service Request Messages (SRM): only client can send SRM messages to server once the connection is fully established.
Servers identify and process both the type of messages very differently.

4. Socket Design (State machine of Client Server Communication)
- When Server boots up, it creates a connection socket (also called "master socket file descriptor" using socket())
        M = socket()
- M is the mother of all Client Handles. M gives birth to all Client Handles. 
Client Handles are also called "data_sockets"
- M is only used to create new client handles. M is not used for data exchange with already connected clients.

     M <--------- New connection request ------------------------------------------S1
      ---- accept() ---- create -----> C1 (Client Handle) <-------Data exchange ---

- accept() is the system call used on server side to create client handles
- Handles are called as "file descriptors" which are just positive integer numbers (int fd > 0)
- Client Handles are called "communication file descriptors" or "Data Sockets"
- M is called "Master socket file descriptor" Or "Connection socket"

5. Unix Domain Sockets (UDS)
- UDS is used for carrying out IPC between 2 processes running on SAME machine (Client <-> Server)
- Using UDS, we can setup STREAM or DATAGRAM based communication
+ STREAM: for large files, or important data, ensure correct sequence data flow (e.g., control signal)
+ DATAGRAM: for small units of Data, large quantities continuously over a certain period of time, 
            Some data might be lost, but this won't affect the overall (e.g., video stream).

6. Multiplexing IO
- Multiplexing is a mechanism through which the Server process can monitor multiple clients at the same time
- select(): monitor all Clients activity at the same time
- Server-process has to maintain Client Handles (communication FDs) to carry out communication (data exchange) with connected clients.
In addition, it has to maintain connection socket (Master socket FD) as well (M) to process new connection request from new clients.
--> Linux provides an inbuilt Data Structure to maintain the set of sockets file descriptors (FDs): fd_set
--> select() system call monitor all socket FDs present in fd_set (M, C1, C2, ...)
