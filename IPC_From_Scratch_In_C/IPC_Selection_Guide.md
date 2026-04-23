# IPC Selection Guide — When to Use What

> A practical guide for choosing the right IPC mechanism in Linux, especially for **Embedded / Automotive** systems.

---

## 1. Overview — All Linux IPC Mechanisms at a Glance

| IPC Mechanism | Scope | Data Model | Speed | Synchronization | Complexity |
|---|---|---|---|---|---|
| **Pipe** | Parent ↔ Child | Byte stream (unidirectional) | ★★★★☆ | Built-in (blocking R/W) | Very Low |
| **Named Pipe (FIFO)** | Same machine | Byte stream (unidirectional) | ★★★★☆ | Built-in (blocking R/W) | Low |
| **Signals** | Same machine | Notification only (no data) | ★★★★★ | Async, interrupt-driven | Low |
| **Message Queue** | Same machine | Discrete messages (queued) | ★★★☆☆ | Built-in (blocking send/recv) | Medium |
| **Shared Memory** | Same machine | Raw memory region (latest value) | ★★★★★ | ❌ None — need semaphore/mutex | Medium-High |
| **Semaphore** | Same machine | Counter (sync primitive only) | ★★★★★ | IS the synchronization | Low |
| **Unix Domain Socket** | Same machine | Byte stream or datagram | ★★★★☆ | Built-in (blocking R/W) | Medium |
| **Network Socket (TCP/UDP)** | Cross-machine | Byte stream (TCP) / Datagram (UDP) | ★★☆☆☆ | Built-in | Medium-High |
| **D-Bus** | Same machine | Method calls + signals (high-level) | ★★☆☆☆ | Built-in | High |
| **Binder** | Same machine (Android) | RPC (Remote Procedure Call) | ★★★★☆ | Built-in | High |

---

## 2. Speed Comparison — Latency & Throughput

```
Fastest ──────────────────────────────────────────────────── Slowest

Shared Memory    Pipe/FIFO    Unix Socket    Msg Queue    TCP Socket    D-Bus
  ~0 copy         1 copy       1-2 copy      1-2 copy     N copies     N copies
  ~ns latency     ~μs          ~μs           ~μs          ~ms          ~ms
```

### Why is Shared Memory the fastest?
```
Normal IPC (e.g., Socket):
  Process A  ──copy──▶  Kernel Buffer  ──copy──▶  Process B
                         (2 copies)

Shared Memory:
  Process A  ──write──▶  Shared Region  ◀──read──  Process B
                          (0 copies — same physical pages)
```

---

## 3. Decision Flowchart

```
                    ┌──────────────────────────┐
                    │  Do the 2 processes run   │
                    │  on the SAME machine?     │
                    └─────────┬────────────────┘
                         Yes  │           No
                              ▼            ▼
                    ┌────────────┐   ┌──────────────────┐
                    │ Same machine│   │ Network Socket   │
                    │ IPC         │   │ TCP/UDP          │
                    └──────┬─────┘   │ or SOME/IP (Auto)│
                           │         └──────────────────┘
                           ▼
              ┌──────────────────────────┐
              │ What kind of data?       │
              └──────┬──────┬──────┬─────┘
                     │      │      │
         Notification│  Stream│  Discrete│   Bulk/Real-time
         (no data)   │  (bytes)│  messages│   (zero-copy)
              ▼      │      ▼      ▼            ▼
          ┌───────┐  │ ┌────────┐ ┌─────────┐ ┌──────────────┐
          │Signal │  │ │Unix    │ │Message  │ │Shared Memory │
          │       │  │ │Socket  │ │Queue    │ │+ Semaphore   │
          └───────┘  │ │or Pipe │ └─────────┘ └──────────────┘
                     │ └────────┘
                     ▼
              ┌─────────────────────┐
              │ Need multiplexing   │
              │ (multiple clients)? │
              └──────┬──────────────┘
                 Yes │        No
                  ▼  │         ▼
           Unix Domain│     Pipe / FIFO
           Socket     │
           + select() │
```

---

## 4. When to Use What — By Use Case

### 4.1 Configuration Update (e.g., user changes settings)

> **→ Shared Memory + Semaphore** (Publisher-Subscriber pattern)

```
User changes IP config → Publisher writes to SHM → sem_post() → All services read new config
```

- ✅ Fastest — zero-copy read
- ✅ Multiple readers, one writer
- ✅ Readers always get latest value
- ❌ Not suitable if you need message history

**Real-world**: Routing table update, network config change, sensor calibration parameters

---

### 4.2 Command / Request-Response (e.g., "start recording", "get status")

> **→ Message Queue** or **Unix Domain Socket**

```
Controller ──"START_RECORD"──▶ Camera Service
Controller ◀──"OK, started"──  Camera Service
```

- Message Queue: simple, reliable, messages are **queued** (won't lose if receiver is busy)
- Unix Socket: more flexible, supports **bidirectional** communication + `select()` multiplexing

**Real-world**: ECU command dispatch, service start/stop, diagnostics request

---

### 4.3 Camera / Video Streaming (high bandwidth, real-time)

> **→ Shared Memory** (zero-copy frame buffer)

```
Camera Driver ──write frame──▶ SHM (1920x1080x3 = 6MB per frame)
                                ▲
Display Service ──read frame────┘  (zero-copy, mmap same pages)
ADAS Service ──read frame───────┘
```

- ✅ Zero-copy — 6MB frame, no kernel buffer copy
- ✅ Multiple consumers can mmap the same region
- ❌ Need synchronization (double-buffering + semaphore)
- ❌ Only works on same machine

**Real-world**: V4L2 camera → GPU processing pipeline, IVI display rendering

**Design pattern — Double Buffer**:
```
┌──────────┐     ┌──────────┐
│ Buffer A │     │ Buffer B │     Camera writes to A → flip → readers read A
│ (write)  │ ──▶ │ (read)   │     Camera writes to B → flip → readers read B
└──────────┘     └──────────┘     Never read/write same buffer simultaneously
```

---

### 4.4 Process Lifecycle Notification (e.g., "child died", "timer expired")

> **→ Signals**

```
Kernel ──SIGCHLD──▶ Parent Process      (child process terminated)
Kernel ──SIGALRM──▶ Watchdog Service    (timer expired)
Process A ─SIGUSR1─▶ Process B          (custom notification)
```

- ✅ Asynchronous — doesn't block sender or receiver
- ✅ Zero overhead — kernel-level delivery
- ❌ Cannot carry data (only signal number)
- ❌ Can be lost if same signal sent multiple times before handling

**Real-world**: Watchdog timer, graceful shutdown (`SIGTERM`), log rotation (`SIGHUP`)

---

### 4.5 Communication Between ECUs (different hardware)

> **→ Network Socket (TCP/UDP)** or **CAN bus (SocketCAN)**

#### Via Ethernet (high bandwidth):
```
Head Unit ──SOME/IP over TCP/UDP──▶ TCU (Telematics)
ADAS ECU ──DDS over UDP multicast──▶ Planning ECU
```

#### Via CAN bus (low bandwidth, safety-critical):
```
ECU A ──CAN frame (8 bytes)──▶ CAN Bus ──▶ ECU B (ABS)
                                         ──▶ ECU C (Airbag)
```

| | Ethernet + SOME/IP | CAN bus |
|---|---|---|
| Bandwidth | 100 Mbps+ | 500 Kbps - 1 Mbps |
| Frame size | Unlimited (TCP) | 8 bytes (classic) / 64 bytes (CAN FD) |
| Use case | Camera, OTA, infotainment | Brake, engine, airbag |
| Addressing | IP:Port | CAN ID (11-bit or 29-bit) |

---

### 4.6 Client-Server with Multiple Clients (e.g., daemon serving N clients)

> **→ Unix Domain Socket + select()/poll()/epoll()**

```
Client 1 ──connect──▶ ┌──────────────┐
Client 2 ──connect──▶ │ Server       │
Client 3 ──connect──▶ │ (select loop)│
                       └──────────────┘
```

- ✅ Bidirectional, reliable (SOCK_STREAM)
- ✅ Multiplexing — one thread handles N clients
- ✅ File descriptor based — works with `select()`/`epoll()`

**Real-world**: systemd, D-Bus daemon, X11/Wayland compositor

---

### 4.7 Parent-Child Process Communication

> **→ Pipe (anonymous)**

```c
int fd[2];
pipe(fd);
fork();
// Parent writes to fd[1], Child reads from fd[0]
```

- ✅ Simplest IPC — no naming, no cleanup
- ❌ Only works between parent and child (inherited fd)
- ❌ Unidirectional (need 2 pipes for bidirectional)

**Real-world**: Shell pipelines (`ls | grep | sort`), subprocess stdout capture

---

## 5. Comprehensive Comparison Table

| Criteria | Pipe | Signal | Msg Queue | Shared Mem | Unix Socket | Network Socket |
|---|---|---|---|---|---|---|
| **Direction** | Uni | Uni | Uni | Multi-reader | Bi | Bi |
| **Data size** | Stream | 0 (no data) | Discrete msg | Arbitrary | Stream/Dgram | Stream/Dgram |
| **Persistence** | No | No | Yes (until read) | Yes (until unlink) | No | No |
| **Cross-machine** | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| **Multiplexing** | select() | sigaction | select() | ❌ | select()/epoll | select()/epoll |
| **Kernel copies** | 1 | 0 | 1-2 | 0 | 1-2 | 2+ |
| **Max throughput** | ~3 GB/s | N/A | ~500 MB/s | ~10+ GB/s | ~3 GB/s | ~1 GB/s |
| **Latency** | ~1 μs | ~0.5 μs | ~2 μs | ~10 ns | ~1 μs | ~50 μs+ |
| **Built-in sync** | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ |

> *Throughput numbers are approximate — measured on typical x86 Linux, actual performance varies by hardware.*

---

## 6. Automotive / Embedded — IPC Map

```
┌──────────────────────────────────────────────────────────────────┐
│                        VEHICLE NETWORK                           │
│                                                                  │
│  ┌─────────────┐    Ethernet     ┌─────────────┐                │
│  │  Head Unit  │◀──(SOME/IP)───▶│  TCU         │                │
│  │  (Android/  │    (TCP/UDP)    │  (Telematics)│                │
│  │   Linux)    │                 └─────────────┘                │
│  │             │    Ethernet     ┌─────────────┐                │
│  │  Internal:  │◀──(DDS/UDP)───▶│  ADAS ECU   │                │
│  │  ┌────────┐ │                 │  (Camera,   │                │
│  │  │Camera  │ │    CAN bus      │   Lidar)    │                │
│  │  │Service │ │◀──(SocketCAN)─▶│             │                │
│  │  └───┬────┘ │                 └─────────────┘                │
│  │      │ SHM  │                                                 │
│  │      │(zero │    CAN bus      ┌─────────────┐                │
│  │  ┌───▼────┐ │◀──(SocketCAN)─▶│  Body ECU   │                │
│  │  │Display │ │                 │  (Doors,    │                │
│  │  │Service │ │                 │   Lights)   │                │
│  │  └────────┘ │                 └─────────────┘                │
│  │      │      │                                                 │
│  │  Unix Socket│    CAN bus      ┌─────────────┐                │
│  │  (commands) │◀──(SocketCAN)─▶│  Powertrain │                │
│  │      │      │                 │  (Engine,   │                │
│  │  ┌───▼────┐ │                 │   ABS)      │                │
│  │  │Diag    │ │                 └─────────────┘                │
│  │  │Service │ │                                                 │
│  │  └────────┘ │                                                 │
│  └─────────────┘                                                 │
│                                                                  │
│  WITHIN Head Unit:                BETWEEN ECUs:                  │
│  ├─ SHM        → camera frames   ├─ Ethernet/SOME/IP → services│
│  ├─ Unix Socket → client-server   ├─ CAN bus → safety signals   │
│  ├─ Msg Queue  → event dispatch   ├─ DoIP/UDS → diagnostics     │
│  ├─ Signals    → lifecycle mgmt   └─ DDS → real-time sensor     │
│  └─ Binder     → Android apps                                   │
└──────────────────────────────────────────────────────────────────┘
```

---

## 7. Quick Decision Rules for Tech Leads

### ✅ DO

| Rule | IPC Choice |
|---|---|
| Need **zero-copy** for large data (video, point cloud) | Shared Memory |
| Need **reliable ordered** message delivery | Message Queue or TCP Socket |
| Need **request-response** pattern | Unix Domain Socket |
| Need **multiple clients** connecting to a server | Unix Socket + `epoll()` |
| Need to **notify** a process (no data) | Signal (`SIGUSR1`, `SIGUSR2`) |
| Need **cross-ECU** communication | Network Socket (SOME/IP / DDS) |
| Need **safety-critical, low-latency** bus | CAN bus (SocketCAN) |
| Need **parent-child** simple pipe | `pipe()` |

### ❌ DON'T

| Anti-pattern | Why it's wrong | Better choice |
|---|---|---|
| Using Socket to transfer 4K video frames on same machine | 2 kernel copies per frame = waste CPU | Shared Memory (zero-copy) |
| Using Shared Memory for command/control messages | No built-in ordering, need manual sync | Message Queue |
| Using Signals to send data | Signals carry NO payload | Message Queue or Socket |
| Using Message Queue for cross-machine communication | POSIX MQ is local-only | Network Socket |
| Busy-polling Shared Memory for changes | 100% CPU waste | Semaphore notification |
| Using single IPC for everything | Each has trade-offs | Mix based on requirements |

---

## 8. Summary — One-liner Decision

```
"Tôi cần gửi ___"                          → Dùng ___

Notification (không data)                   → Signal
Lệnh / command (text, struct nhỏ)          → Message Queue
Request-Response (client-server)            → Unix Domain Socket
Video frame / large buffer (zero-copy)      → Shared Memory + Semaphore
Dữ liệu giữa 2 máy khác nhau              → Network Socket (TCP/UDP)
Tín hiệu an toàn giữa ECU (phanh, airbag)  → CAN bus
Service discovery + RPC giữa ECU            → SOME/IP
Real-time sensor fusion (ADAS)              → DDS
Simple parent → child output                → Pipe
```

---

*Author: sanghandsondev — Part of [linux_system_learning](https://github.com/sanghandsondev/linux_system_learning)*
