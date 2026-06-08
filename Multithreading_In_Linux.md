# Multithreading trong Linux

## 1) Process vs Thread: Linux không phân biệt nhiều

Kernel Linux quản lý mọi thứ qua **task** (task_struct). Process và thread đều là task — khác nhau ở chỗ thread **chia sẻ address space**, file descriptor, signal handler với nhau; còn process thì không.

- Thread cùng process đọc/ghi chung vùng nhớ trực tiếp → nhanh hơn IPC, nhưng cần đồng bộ.
- Context switch giữa 2 thread cùng process rẻ hơn 2 process vì không cần đổi page table (mm_struct).

### fork() vs clone()

- `fork()` tạo process mới, copy address space (copy-on-write).
- `clone()` là syscall nền mà `pthread_create` dùng — cho phép chỉ định tài nguyên nào chia sẻ (flag `CLONE_VM`, `CLONE_FILES`, `CLONE_SIGHAND`...).
- Thread thực chất là `clone()` với đủ các flag chia sẻ.

---

## 2) Scheduler: ai được chạy

Linux có 3 tầng ưu tiên:
1. **Deadline** – deadline cứng
2. **Real-time** (FIFO/RR) – ưu tiên cao, dùng cho RT task
3. **CFS** – mặc định cho app thường

### CFS (Completely Fair Scheduler)

- Mỗi CPU có runqueue riêng.
- Mỗi task có **vruntime** – thời gian CPU đã dùng (điều chỉnh theo nice weight).
- Task có vruntime **nhỏ nhất** được chọn chạy tiếp.
- Khi task migrate sang CPU khác → mất cache locality.

### Preemption

- **Voluntary**: thread tự yield (block I/O, futex wait, `sched_yield()`).
- **Involuntary**: kernel preempt khi hết time slice hoặc task ưu tiên cao hơn wakeup.

I/O-heavy thread thường voluntary sleep nhiều → vruntime thấp → khi wakeup được ưu tiên cao hơn CPU-heavy thread.

---

## 3) Context switch: tốn gì

Khi timer interrupt hoặc syscall kích hoạt:

1. Lưu register + stack pointer của task đang chạy.
2. Scheduler chọn task kế tiếp từ runqueue.
3. Nạp state của task mới, return to user mode.

**Chi phí thực sự** không chỉ save/restore register:

```
T_switch = T_save_restore + T_sched + T_cache_cold + T_tlb + T_migration
```

- **Cache cold**: working set mới chưa có trong L1/L2 → cache miss tăng.
- **TLB flush**: nếu switch sang process khác (khác address space).
- **CPU migration**: nếu task chạy trên core khác, data không còn local.

Context switch giữa 2 thread cùng process không cần TLB flush (cùng mm_struct), nhưng vẫn bị cache cold nếu working set khác nhau.

---

## 4) Trạng thái task

```
Runnable → Running → Sleeping (I/O, futex wait) → Runnable (wakeup) → Running
```

- **Running**: đang dùng CPU.
- **Runnable**: sẵn sàng, đang chờ CPU.
- **Sleeping interruptible**: chờ event, signal có thể đánh thức.
- **Sleeping uninterruptible (D state)**: chờ I/O sâu, không kill được bằng signal thường.

D state đáng chú ý: nếu `top` hoặc `ps` thấy nhiều process D state → thường là vấn đề I/O hoặc NFS/kernel bug.

---

## 5) Đồng bộ: mutex, spinlock, và futex

### Mutex vs Spinlock

| | Mutex | Spinlock |
|---|---|---|
| Khi contention | Sleep (vào kernel) | Busy-wait (vẫn trên CPU) |
| Chi phí | Context switch | Burn CPU cycles |
| Dùng khi | Critical section dài, hoặc có thể sleep | Critical section **cực ngắn**, không thể sleep (kernel context) |

Trong user-space C++: hầu như luôn dùng `std::mutex`. Spinlock chỉ hợp lý khi critical section vài chục nanoseconds và contention thấp.

### Futex — cơ chế nền của mutex

Mutex hiện đại dùng 2 tầng:

1. **Fast path**: atomic CAS ở user-space — không vào kernel nếu lock rảnh.
2. **Slow path** (contention): gọi `futex_wait` → thread ngủ trong kernel queue.

Khi unlock: `futex_wake` → thread chờ vào Runnable → scheduler cấp CPU.

**Kết luận**: lock contention = context switch + scheduler overhead. Càng nhiều contention, overhead càng lớn.

### Read-Write Lock

`std::shared_mutex` (C++17):
- Nhiều reader có thể chạy song song.
- Writer cần exclusive lock — block tất cả reader và writer khác.
- Hợp lý khi read >> write và critical section đọc nặng.
- **Lưu ý**: writer starvation có thể xảy ra nếu liên tục có reader mới vào.

### Condition Variable

`std::condition_variable` kết hợp với mutex:

```cpp
// Producer
{
    std::lock_guard lk(mtx);
    queue.push(item);
}
cv.notify_one();

// Consumer
std::unique_lock lk(mtx);
cv.wait(lk, []{ return !queue.empty(); });  // predicate tránh spurious wakeup
```

Internally: `wait()` unlock mutex → futex_wait → khi notify, futex_wake → reacquire mutex.

**Spurious wakeup**: thread có thể tỉnh giấc mà không có `notify`. Luôn dùng predicate form `cv.wait(lk, predicate)`.

---

## 6) False sharing và Cache Coherence

### Cache coherence (MESI protocol)

Mỗi cache line có trạng thái: Modified, Exclusive, Shared, Invalid.

- Core A ghi vào cache line → báo cho các core khác → cache line của chúng chuyển sang Invalid.
- Core B muốn đọc lại → cache miss → phải fetch từ L3 hoặc RAM.
- Đây là **cache coherence traffic** — xảy ra kể cả khi không có lock.

### False sharing

Hai thread ghi **hai biến khác nhau** nhưng nằm trên **cùng một cache line** (64 bytes):

- Mỗi lần một core ghi → invalidate cache line của core kia.
- Core kia phải fetch lại từ L3 hoặc RAM → chậm dù không có lock.

**Fix**: padding/alignment để tách các biến ra khác cache line.

```cpp
struct alignas(64) Counter { std::atomic<int> val; };
```

Hay gặp trong: per-thread counter, lock-free queue, ring buffer nhiều producer/consumer.

---

## 7) Memory Ordering và Atomic

`std::atomic` không chỉ là "ghi/đọc an toàn" — quan trọng hơn là **thứ tự quan sát** giữa các thread.

| Order | Ý nghĩa |
|---|---|
| `relaxed` | Chỉ đảm bảo atomicity, không đảm bảo thứ tự |
| `acquire` | Không có load/store nào sau nó bị reorder lên trước |
| `release` | Không có load/store nào trước nó bị reorder xuống sau |
| `seq_cst` | Tất cả thread thấy cùng thứ tự — mạnh nhất, đắt nhất |

**Rule of thumb cho Middleware**:
- Flag check đơn giản: `relaxed` đủ nếu có acquire/release ở chỗ khác bảo vệ.
- Producer/consumer với shared buffer: `release` khi ghi, `acquire` khi đọc.
- Không chắc: dùng `seq_cst` — đúng nhưng chậm hơn.

```cpp
// Producer
data.store(value, std::memory_order_relaxed);
ready.store(true, std::memory_order_release);  // đảm bảo data ghi trước ready

// Consumer
while (!ready.load(std::memory_order_acquire));  // thấy ready=true thì data đã sẵn
auto v = data.load(std::memory_order_relaxed);
```

**`volatile` ≠ `atomic`**: `volatile` chỉ ngăn compiler optimize đọc/ghi, không có bảo đảm atomicity hay memory ordering. Không dùng `volatile` cho thread communication trong C++.

---

## 8) Oversubscription và Thread Pool

### Oversubscription

Số thread runnable >> số logical CPU → hệ thống ì:

- Context switch dày đặc.
- Cache miss tăng.
- Tail latency xấu.

**Nguyên tắc**:
- **CPU-bound**: số worker ≈ số logical CPU.
- **I/O-bound**: có thể nhiều hơn, nhưng phải đo thực tế.

### Thread Pool

Thay vì tạo/hủy thread theo từng request → dùng pool cố định:

- Tránh overhead `clone()` + stack allocation mỗi lần.
- Giữ số thread ổn định, tránh oversubscription.
- Task queue thường là lock-free hoặc dùng mutex + condition variable.

Pattern cơ bản: N worker thread ngủ, wakeup khi có task, lấy task từ queue, xử lý, ngủ lại.

---

## 9) Các vấn đề đồng bộ thường gặp

### Deadlock

Xảy ra khi 2 thread cầm lock của nhau và đợi nhau mãi.

**Phòng tránh**:
- Luôn acquire lock theo **thứ tự cố định** — ví dụ lock A trước B trong toàn bộ code.
- Dùng `std::lock(a, b)` hoặc `std::scoped_lock(a, b)` — acquire cả 2 atomic, tránh deadlock.
- Tránh gọi callback hay virtual function khi đang giữ lock — callback có thể acquire lock khác.

### Priority Inversion

Thread ưu tiên thấp giữ lock → thread ưu tiên cao phải chờ → hiệu ứng: ưu tiên bị "đảo ngược".

Giải pháp: **Priority Inheritance** — kernel tạm thời boost priority của thread đang giữ lock lên bằng thread đang chờ. `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)`.

### Lock Convoy

Nhiều thread cùng tranh 1 lock → thread vừa release lock bị preempt ngay → thread khác wakeup, acquire lock, lại bị preempt → cả đoàn thread chờ nhau lần lượt → throughput thảm.

Dấu hiệu: lock contention cao nhưng thực ra không ai giữ lock lâu.

### ABA Problem

Xảy ra với lock-free CAS:
1. Thread đọc giá trị A.
2. Thread khác đổi A → B → A.
3. Thread gốc CAS thành công dù pointer A đã là object khác.

Fix: dùng **tagged pointer** — kèm counter tăng dần vào trong giá trị atomic.

### Thundering Herd

`notify_all()` đánh thức nhiều thread cùng lúc, nhưng chỉ 1 thread thắng lock, còn lại ngủ lại → lãng phí context switch.

Dùng `notify_one()` khi có thể. Chỉ dùng `notify_all()` khi thực sự tất cả thread cần xem xét lại điều kiện.

---

## 10) NUMA (nếu bị hỏi)

Trên server nhiều socket:

- RAM gần socket nào thì core ở socket đó truy cập nhanh hơn (local access).
- Thread migrate sang socket khác → remote memory access → latency cao hơn.
- Low-latency system thường **pin thread** vào CPU set cố định (`pthread_setaffinity_np` hoặc `taskset`).
- `numactl` để bind process vào NUMA node cụ thể.

---

## 11) Thread-local Storage

`thread_local` biến mỗi thread có bản sao riêng — không cần sync.

```cpp
thread_local uint64_t thread_id = 0;
thread_local std::vector<int> per_thread_buffer;
```

Dùng tốt cho: per-thread cache, per-thread random seed, per-thread allocator.

---

## 12) Signal và Multithreading

- Signal được deliver tới **một thread nào đó** trong process, không đảm bảo thread nào.
- `SIGSEGV`, `SIGFPE` deliver đến thread gây ra fault.
- Cách an toàn: dùng `sigwait()` hoặc `signalfd()` ở một thread chuyên xử lý signal, các thread còn lại block hết signal bằng `pthread_sigmask`.

---

## 13) Những câu hỏi interview hay gặp

### Khái niệm cơ bản

**"Process và thread khác nhau thế nào ở mức kernel Linux?"**
→ Đều là task (task_struct). Thread chia sẻ address space, fd table, signal handler với nhau trong cùng process. Process thì không. Clone() với flag CLONE_VM tạo thread, fork() copy address space.

**"Thread switch và process switch khác nhau chỗ nào?"**
→ Thread cùng process không cần TLB flush (cùng mm_struct), chỉ save/restore register và stack. Process switch phải flush TLB, overhead lớn hơn. Nhưng thread switch vẫn bị cache cold nếu working set khác nhau.

**"Kernel thread và user thread là gì?"**
→ Kernel thread: task chạy hoàn toàn trong kernel space, không có user-space counterpart. User thread: thread của ứng dụng, được map 1:1 với kernel task trên Linux (NPTL model). Không còn M:N hay green thread ở mức POSIX trên Linux hiện đại.

**"Daemon thread là gì?"**
→ Thread chạy nền, không ngăn process exit. Trong C++: `t.detach()`. Khi main thread kết thúc, daemon thread bị kill. Nguy hiểm nếu đang giữ lock hoặc chưa flush buffer.

---

### Scheduler và Performance

**"Tăng thread là tăng performance?"**
→ Không. Giới hạn bởi số core (CPU-bound) và overhead context switch/contention. Amdahl's Law: nếu 20% code là sequential thì dù vô hạn thread, speedup tối đa chỉ 5x.

**"Thread switch có tốn không?"**
→ Có. Cache cold + TLB (nếu khác process) + scheduler decision. Không phải chỉ save/restore register. Voluntary switch (block I/O) thường rẻ hơn involuntary (timer preempt) vì thread tự nguyện nhường thường đã không còn nhiều data trên cache.

**"8 core thì dùng 100 thread CPU-bound được không?"**
→ Không. Oversubscription gây context switch storm. 92 thread chờ CPU, liên tục switch qua lại, cache liên tục bị invalidate. Throughput giảm, latency tăng.

**"Làm sao đo số context switch đang xảy ra?"**
→ `vmstat 1` (cs column), `pidstat -w -p <pid>`, `/proc/<pid>/status` (voluntary_ctxt_switches + nonvoluntary_ctxt_switches).

**"CFS là gì, vì sao I/O-bound thread thường có latency thấp hơn CPU-bound?"**
→ CFS track vruntime. I/O thread thường xuyên sleep → vruntime tích lũy chậm → khi wakeup có vruntime nhỏ → được ưu tiên chạy sớm hơn thread CPU-bound đang ngốn CPU liên tục.

---

### Mutex, Lock, Sync

**"Mutex và spinlock: khi nào dùng cái nào?"**
→ Mutex khi critical section dài hoặc có thể sleep → kernel sleep/wakeup. Spinlock khi critical section vài chục ns, không được sleep (interrupt context, kernel code). Trong user-space C++ hầu như luôn mutex; spinlock chỉ hợp lý với low-contention + cực ngắn.

**"Mutex hoạt động thế nào khi không có contention?"**
→ Fast path: atomic CAS ở user-space (futex). Nếu lock rảnh thì xong, không vào kernel. Chi phí chỉ là một atomic instruction. Khi có contention mới gọi futex_wait → vào kernel → sleep.

**"Condition variable, tại sao phải dùng loop/predicate thay vì if?"**
→ Spurious wakeup — thread có thể tỉnh giấc không có notify. Và nếu có 2 thread wait cùng cv, notify_all đánh thức cả 2 nhưng chỉ 1 thread có thể lấy được điều kiện. Thread kia phải wait lại.

**"Deadlock phòng tránh thế nào?"**
→ Lock ordering cố định. Dùng `std::scoped_lock` khi cần acquire nhiều lock cùng lúc. Không gọi callback/virtual function khi đang giữ lock. Lock timeout (`try_lock_for`) để detect deadlock.

**"Priority inversion là gì?"**
→ Thread ưu tiên thấp giữ lock, thread ưu tiên cao phải chờ. Trong hệ thống RT có thể dùng priority inheritance mutex (`PTHREAD_PRIO_INHERIT`) — kernel tạm boost priority của thread đang giữ lock.

**"Read-write lock khi nào tốt hơn mutex?"**
→ Khi read nhiều, write ít, và critical section đọc đủ nặng để amortize overhead của rw_lock. Nếu read rất ngắn (< 100ns), overhead quản lý reader count đôi khi còn đắt hơn plain mutex.

---

### Atomic và Memory Model

**"volatile và atomic khác nhau thế nào?"**
→ `volatile` chỉ ngăn compiler tối ưu hóa đọc/ghi (dùng cho MMIO). Không có bảo đảm atomicity, không có memory ordering. `std::atomic` đảm bảo cả hai. Không dùng `volatile` cho thread communication trong C++.

**"seq_cst, acquire/release, relaxed — khi nào dùng gì?"**
→ `relaxed`: chỉ cần atomicity, không cần ordering (counter đơn giản). `acquire/release`: producer-consumer pattern, đảm bảo data visible trước khi flag visible. `seq_cst`: cần total order toàn bộ hệ thống — đúng nhưng có thể 2-3x chậm hơn trên ARM.

**"Memory barrier là gì?"**
→ Instruction ngăn CPU và compiler reorder memory operation qua barrier. Acquire barrier: không reorder load về trước. Release barrier: không reorder store về sau. `std::atomic` với memory order tự chèn barrier phù hợp.

**"ABA problem là gì?"**
→ Lock-free CAS: đọc A, context switch, thread khác đổi A→B→A, CAS thành công dù pointer A là object mới. Fix: tagged pointer — giá trị atomic gồm pointer + monotonic counter, CAS fail nếu counter khác dù pointer giống.

---

### Design và Patterns

**"Thread pool tốt hơn spawn thread theo request ở điểm nào?"**
→ Tránh overhead `clone()` + stack allocation + kernel task setup mỗi lần. Giữ số thread ổn định. Warm cache. Có thể limit concurrency để tránh oversubscription.

**"Lock-free khi nào nên dùng?"**
→ Khi benchmark chứng minh mutex là bottleneck thực sự. Lock-free code phức tạp, khó debug, dễ sai memory ordering. Không dùng chỉ vì "nghe có vẻ nhanh hơn". Nhiều "lock-free" code thực ra chỉ là wait-free ở fast path còn có fallback lock.

**"Làm sao implement producer-consumer queue an toàn?"**
→ Đơn giản: `std::queue` + `std::mutex` + `std::condition_variable`. Consumer wait khi empty, producer notify khi push. Nếu cần hiệu năng cao: lock-free ring buffer với atomic head/tail (nhưng chỉ khi đây thực sự là bottleneck).

**"False sharing gặp ở đâu hay nhất trong Middleware?"**
→ Per-thread counter trong struct chung, cache line trong ring buffer nhiều producer, hot field nằm cạnh cold field trong cùng struct. Fix: `alignas(64)`, tách field hay update thành struct riêng, hoặc padding.

**"Thundering herd là gì và fix thế nào?"**
→ `notify_all()` đánh thức nhiều thread nhưng chỉ 1 thắng lock. Còn lại sleep lại → lãng phí N-1 context switch. Fix: dùng `notify_one()` khi chỉ 1 thread cần xử lý. Dùng `notify_all()` khi điều kiện thay đổi ảnh hưởng tất cả waiter.

**"Signal trong multithreaded process nên xử lý thế nào?"**
→ Block hết signal ở tất cả worker thread bằng `pthread_sigmask`. Tạo một thread chuyên dùng `sigwait()` hoặc `signalfd()` để xử lý. Tránh gọi non-async-signal-safe function trong signal handler.

---

## 14) Một câu tóm tắt

> Multithreading trên Linux: nhiều task cạnh tranh CPU time. Hiệu năng thực tế phụ thuộc vào scheduler, hành vi block/wakeup, cache locality — không phải số lượng thread.
