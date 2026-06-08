# Bộ nhớ trên Linux cho C++ Middleware (phiên bản dễ hiểu, có số liệu)

Mục tiêu: nắm chắc phần Memory và Virtual Memory đủ để làm việc thực tế, tối ưu hiệu năng và trả lời phỏng vấn Senior C++ mà không sa đà quá sâu vào chi tiết kernel.

## 1) Bức tranh tổng thể

Linux cho mỗi tiến trình một không gian địa chỉ ảo riêng.

- Ứng dụng đọc/ghi địa chỉ ảo.
- MMU dịch sang địa chỉ vật lý qua page table.
- Mỗi tiến trình tách biệt nhau, an toàn hơn.

Các con số nền tảng nên nhớ:

- Kích thước page phổ biến: 4 KB.
- Huge page thường gặp: 2 MB, đôi khi 1 GB.
- Cache line trên đa số CPU x86_64 hiện nay: 64 byte.

## 2) Virtual Memory hoạt động thế nào

Luồng cơ bản:

1. Thread truy cập một địa chỉ ảo.
2. CPU tra TLB.
3. Nếu TLB miss thì page table walk.
4. Nếu chưa có mapping hợp lệ thì page fault vào kernel.

Phân loại page fault:

- Minor fault: thường nhanh, có thể chỉ vài trăm ns đến vài us.
- Major fault: phải đọc dữ liệu từ đĩa hoặc swap, có thể từ hàng trăm us đến vài ms, thậm chí tệ hơn khi I/O bận.

## 3) Các vùng nhớ trong tiến trình C++

- Text: mã máy.
- Data/BSS: biến global, static.
- Heap: new, malloc.
- Stack: biến cục bộ, call frame.
- mmap region: shared library, file mapping, anonymous mapping.

Tóm tắt nhanh bằng chữ thay cho sơ đồ:

- Không gian địa chỉ user thường gồm: text -> data/BSS -> heap -> mmap region -> stack.
- Heap thường mở rộng lên phía địa chỉ cao hơn, stack thường mở rộng xuống phía địa chỉ thấp hơn.
- CPU truy cập địa chỉ ảo, MMU tra TLB/page table để dịch sang địa chỉ vật lý.
- Nếu chưa có mapping hợp lệ sẽ phát sinh page fault, kernel xử lý xong thì tiến trình chạy tiếp.

## 4) Giới hạn Stack, Heap và bộ nhớ tối đa

Các giới hạn bạn cần quan tâm khi vận hành:

- Stack mỗi thread thường mặc định 8 MB trên nhiều distro Linux (kiểm tra thực tế bằng ulimit).
- Khi có 1.000 thread, chỉ riêng stack mặc định đã có thể là khoảng 8 GB không gian địa chỉ ảo.
- Heap không có một con số trần cố định cho mọi app.
- Trần thực tế phụ thuộc RLIMIT_AS, RLIMIT_DATA, cgroup/container limit, RAM vật lý, swap, fragmentation và overcommit policy.

Lệnh kiểm tra:

- ulimit -a
- cat /proc/<pid>/limits
- cat /proc/sys/vm/overcommit_memory
- cat /proc/meminfo

## 5) Fragmentation và tác động thực tế

External fragmentation:

- Còn tổng free lớn nhưng rời rạc, khó cấp phát block lớn.

Internal fragmentation:

- Xin 33 byte nhưng allocator cấp lên size class 48 hoặc 64 byte.

Vì sao middleware hay gặp:

- Nhiều object nhỏ, lifetime khác nhau.
- Tạo hủy liên tục theo request.
- Nhiều thread dùng chung allocator gây contention.

Dấu hiệu nhận biết:

- RSS tăng dần, không giảm tương ứng sau khi tải giảm.
- p95/p99 latency xấu dần theo thời gian chạy.
- CPU profile nóng ở malloc/free.

## 6) Buffer overhead trong C++ middleware

### 6.1 Reallocation churn

Hiện tượng:

- vector hoặc string tăng dần từng chút dẫn đến nhiều lần cấp phát lại và copy.

Cách xử lý:

- reserve sớm theo kích thước ước lượng.
- Dùng mô hình tăng trưởng capacity rõ ràng theo workload.

### 6.2 Copy chain nhiều tầng

Hiện tượng:

- Dữ liệu đi qua socket buffer -> parser buffer -> queue buffer -> business object, mỗi tầng lại copy.

Cách xử lý:

- Giảm số lần copy bằng move semantics.
- Dùng span hoặc string_view khi chỉ đọc.
- Parse in-place nếu đảm bảo vòng đời dữ liệu.

### 6.3 Nhiều object nhỏ

Hiện tượng:

- Quá nhiều allocation nhỏ gây overhead metadata và lock contention.

Cách xử lý:

- Object pool hoặc arena theo lifetime.
- Batch allocate, batch free.

## 7) Những vấn đề thật sự cần giải quyết trong C++

1. Tăng số thread nhưng throughput không tăng.
Nguyên nhân thường gặp:
- Contention ở allocator và lock.
- Cache miss, false sharing, context switch tăng.
Hướng xử lý:
- Giảm cấp phát ở hot path.
- Tách dữ liệu nóng theo thread.
- Dùng thread-local buffer hoặc pool.

2. Dùng ít CPU nhưng p99 vẫn cao.
Nguyên nhân thường gặp:
- Major page fault hoặc swap.
- Reallocation gây spike.
Hướng xử lý:
- Theo dõi pgmajfault, swap-in/out.
- Warm-up bộ nhớ, reserve buffer.
- Giới hạn memory footprint tránh chạm swap.

3. RSS tăng liên tục sau nhiều giờ chạy.
Nguyên nhân thường gặp:
- Fragmentation.
- Cache nội bộ phình ra không có cơ chế thu hồi.
Hướng xử lý:
- Thiết kế cơ chế shrink theo chu kỳ.
- Gom lifetime object.
- Đánh giá allocator phù hợp workload.

## 8) Tăng hiệu năng memory: checklist hành động

1. Đo đúng thứ cần đo:
- RSS, VmSize, page fault, alloc/sec, p95 và p99 latency.

2. Giảm cấp phát trong hot path:
- Tái sử dụng buffer.
- Tránh tạo object tạm không cần thiết.

3. Tăng locality:
- Ưu tiên dữ liệu liên tục trong bộ nhớ.
- Thường vector tốt hơn list trong workload thực.

4. Tránh copy thừa:
- Ưu tiên reference, move, emplace.

5. Giảm false sharing:
- Tránh nhiều thread ghi vào biến nằm chung cache line 64 byte.
- Cân nhắc alignas(64) cho counter nóng theo thread.

6. Kiểm soát thread stack:
- Không để hàng ngàn thread với stack mặc định nếu không cần.
- Cân nhắc giảm stack size khi thiết kế thread pool.

7. Tránh swap cho dịch vụ nhạy độ trễ:
- Giữ headroom RAM.
- Cảnh báo sớm khi memory pressure tăng.

## 9) Lệnh Linux quan sát memory khi sự cố

- free -h
- vmstat 1
- pidstat -r -p <pid> 1
- cat /proc/<pid>/status
- cat /proc/<pid>/smaps_rollup
- grep -E 'VmRSS|VmSize|VmSwap|Threads' /proc/<pid>/status

## 10) Q&A phỏng vấn theo tình huống (Senior C++)

### Q1. Dịch vụ tăng p99 từ 5 ms lên 40 ms sau 2 giờ chạy, CPU chỉ 45%. Bạn làm gì?

Trả lời mẫu:

1. Kiểm tra major page fault và swap trước vì đây là nguồn spike lớn.
2. So sánh RSS theo thời gian để phát hiện fragmentation hoặc memory leak.
3. Lấy profile allocator xem malloc/free có nóng không.
4. Nếu do reallocation, thêm reserve và tái sử dụng buffer.
5. Chạy lại benchmark A/B và theo dõi p99.

### Q2. Tại sao còn nhiều RAM nhưng app vẫn OOM trong container?

Trả lời mẫu:

- OOM tính theo cgroup limit của container, không phải toàn máy.
- Có thể tiến trình vượt memory limit dù máy host còn RAM.
- Cần xem memory.current, memory.max và log OOM killer.

### Q3. Bạn tối ưu gì trước: đổi allocator hay sửa code?

Trả lời mẫu:

- Ưu tiên sửa pattern cấp phát trong code trước: reserve, reuse, giảm copy.
- Sau đó mới benchmark allocator thay thế theo workload thật.
- Không đổi allocator nếu chưa có số liệu chứng minh bottleneck.

### Q4. Khi nào dùng memory pool?

Trả lời mẫu:

- Khi object nhỏ, tạo hủy tần suất cao, kích thước tương đối đồng nhất.
- Pool giảm overhead cấp phát và giảm fragmentation.
- Cần thiết kế vòng đời rõ để tránh giữ bộ nhớ quá lâu.

### Q5. Làm sao giải thích vector thường nhanh hơn list?

Trả lời mẫu:

- vector có locality tốt, CPU prefetch tốt, ít cache miss.
- list tốn thêm con trỏ, nhảy node rải rác trong RAM.
- Dù độ phức tạp lý thuyết có lúc tốt, hiệu năng thực tế thường thua vì chi phí bộ nhớ.

### Q6. Nếu xảy ra false sharing thì xử lý sao?

Trả lời mẫu:

1. Xác định biến nóng bị nhiều thread ghi.
2. Tách theo thread-local hoặc tách struct.
3. Canh hàng bộ nhớ theo cache line 64 byte.
4. Đo lại cache miss và throughput để xác nhận hiệu quả.

## 11) Red flags khi review code middleware

- Cấp phát và giải phóng object trong vòng lặp request nóng.
- Ghép chuỗi liên tục mà không reserve.
- Dùng container node-based cho dữ liệu truy cập tuần tự dày đặc.
- Copy payload lớn qua nhiều lớp trừu tượng.
- Không có metric memory ở môi trường production.

## 12) Tóm tắt một câu

Tối ưu Memory trên Linux không chỉ là giảm dung lượng dùng RAM, mà là giảm chi phí cấp phát, giảm page fault, tăng cache locality và ổn định p95/p99 latency.
