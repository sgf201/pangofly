# pangofly 设计（v2）

## 1. 需求

1. **零拷贝**：发端直接在共享内存中构造对象，收端直接使用该对象。
2. **同地址映射**：收发双方映射到同一虚拟地址，收端可直接使用对象内的指针。
3. **支持 non-POD**：消息可包含自定义 vector / string，用户编程方式与 `std::vector` 一致。

## 2. 旧实现为什么做不到

| 位置 | 缺陷 | 后果 |
|------|------|------|
| `core/memory/allocator.h` | `ScopedAllocator` 把资源绑到 thread_local | 换线程 / 构造时机不对就静默落到私有堆，指针跨进程失效 |
| `core/memory/shm_allocator.h` | `ShmAllocator` 默认构造读 thread_local | pmr 要求分配器可默认构造，于是「忘记绑定」永远不报错 |
| `core/memory/shm_resource.h` | `do_deallocate` 是 no-op | `vector` 的 `shrink` / 重分配语义不完整，内存只增不减 |
| `core/transport/shm/posix_segment.cc` | `FIXED_VADDR_STEP = 2MB`，而单通道实际约 16.78MB | 相邻通道 VA 必然重叠 |
| 同上 | `channel_id % 100` 哈希取模 | 不同 channel 撞同一槽位 |
| 同上 | `mmap(nullptr, ...)` 静默回退 | 映射地址不一致，裸指针跨进程无效，且无任何报错 |
| `core/transport/shm/segment.cc` | `AcquireBlockToWrite` 忽略 `msg_size` | 消息体可能超出块容量 |
| 同上 | `GetNextWritableBlockIndex` 用函数内 `static` | 多写者竞争同一索引 |
| `idl/codegen` | `CalculateSize()` 只累加一层 vector | 嵌套 vector / string 漏算 |

结论：这些缺陷不是补丁能修的，根源是「隐式分配器注入 + 静默地址回退」这两个设计选择。v2 把它们换成显式与硬失败。

## 3. 分层

```
IDL (idc)  ──►  类型层  shm::vector / shm::string / shm::Allocator
                   │
                   ▼
              分配层  Arena（块内 TLSF）
                   │
                   ▼
              消息层  BlockHeader / Sample<T> / View<T>
                   │
                   ▼
              传输层  Segment（发布、获取、地址握手）
                   │
                   ▼
              地址层  Region（窗口注册表，MAP_FIXED_NOREPLACE）
```

## 4. 地址层：Region

### 4.1 注册表

注册表放在独立的小 shm（`/pangofly_v2_registry`），固定映射在 `0x580000000000`，
与数据窗口区（默认 `0x600000000000` 起）互不干扰。

```cpp
struct ChannelSlot {
  char name[64];                  // 完整名字，无哈希
  std::atomic<uint32_t> state;    // FREE / ACTIVE
  uint32_t window_index;
  uint32_t span;                  // 连续窗口数；0 表示「延续槽」
  uint64_t mapped_size;
  std::atomic<uint32_t> refcount;
};
```

### 4.2 分配规则

- 每 channel 独占 `window_size`（默认 64MB）整数倍的连续窗口。
- 同名 channel 复用已有窗口并 `refcount++`，保证收发双方拿到同一地址。
- 槽位耗尽 → `Region::Acquire` 返回无效 `Window`，调用方硬失败。

### 4.3 硬失败映射

```cpp
void* MapFixedCreate(int fd, size_t size, uintptr_t addr) {
  // 1. 先探测：Linux 4.17+ 用 MAP_FIXED_NOREPLACE，占用则失败
  void* probe = mmap((void*)addr, size, PROT_NONE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
  if (probe == MAP_FAILED) return nullptr;
  munmap(probe, size);

  // 2. 真正映射，并再次校验返回值等于请求地址
  void* p = mmap((void*)addr, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);
  if (p == MAP_FAILED || (uintptr_t)p != addr) return nullptr;
  return p;
}
```

绝不 `mmap(nullptr, ...)` 回退。

### 4.4 地址握手

`SegmentHeader::self_base` 记录创建者的映射地址。打开者校验：

```cpp
if (hdr->self_base != reinterpret_cast<uint64_t>(mem)) {
  // 地址不一致 → 卸载映射、释放窗口、返回 false
}
```

这样「容器内裸指针跨进程有效」就有了可验证的前提，而不是靠运气。

## 5. 分配层：Arena（块内 TLSF）

每个块内部有一份独立的 arena 元数据，紧贴块 payload 起始。

### 5.1 块布局

```
offset o        : uint32 size|flags    (低 4 位是标志)
offset o+4      : uint32 prev_raw      (前一个物理块的 raw，用于向后合并)
offset o+8      : uint32 next_free     (仅空闲块，相对 base 的偏移)
offset o+12     : uint32 prev_free
offset o+16     : payload 起点
payload 前 8 字节 : uint32 owner_offset, uint32 owner_magic
offset o+size-4 : uint32 footer_raw
```

- 通信开销 16 字节头 + 4 字节尾 = 20 字节，最小块 32 字节。
- 分配按 16 字节对齐；超过 16 的对齐请求走 `kFlagAligned` 分支。

### 5.2 空闲链表用偏移而非指针

`next_free` / `prev_free` 存的是**相对 arena base 的 uint32 偏移**。这样即使
共享内存被映射到不同地址，链表依然有效（虽然 v2 要求同地址，但这是更稳健的选择）。

哨兵用 `kNoOffset = 0xFFFFFFFF`，因为偏移 `0` 是合法的块起点。

### 5.3 TLSF 索引

- 一级 `fl`：`floor(log2(size))`，共 32 级
- 二级 `sl`：每级再分 16 等份
- 位图两级：`fl_bitmap_` + `sl_bitmap_[32]`

`FindBlock()` 沿同桶链表逐块检查 `SizeOf(raw) >= need`——同桶内大小是区间，
不能只看桶头就认定满足。

### 5.4 释放与合并

`Deallocate` 通过 payload 前 8 字节的 owner trailer 反查块偏移，然后：

```
标记 free → 与后继合并 → 与前驱合并 → 插入空闲链表
```

### 5.5 O(1) 整体回收

```cpp
void Arena::Reset() {
  used_ = 0;
  peak_ = 0;
  fl_bitmap_ = 0;
  sl_bitmap_ 清零; free_head_ 全部置 kNoOffset;
  WriteRaw(0, capacity_ | kFlagFree);
  InsertFree(0);
}
```

一块内存的回收与消息里有多少元素**无关**，这正是「不做逐元素析构」的支撑。

### 5.6 验证

ASan + UBSan 下跑 2 万次随机 alloc/free/校验：

```
capacity=1046368 used=0
1000x alloc/free 4096 -> used=0
random stress: 270 live blocks, used=964368 peak=1008464
after free all -> used=0
blocks=1 largest=1046368
ARENA ALL PASS
```

## 6. 类型层：Allocator / vector / string

### 6.1 核心约束

```cpp
template <typename T>
class Allocator {
  Allocator() = delete;                       // ★ 支点
  explicit Allocator(Arena* a) noexcept;
  using is_always_equal = std::false_type;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap            = std::true_type;
};
```

`= delete` 把「忘记绑定 arena」从运行期静默错误变成**编译期错误**：

```
error: use of deleted function 'A<T>::A()'
```

pmr 的 `polymorphic_allocator` 必须能默认构造（取 `get_default_resource()`），
这正是所有隐式 bug 的根源。自定义 Allocator 没有这个包袱，也不依赖
`<memory_resource>`（对 A210 的 GCC 8.3 + uclibc 更友好）。

### 6.2 与标准容器一致的用法

```cpp
template <typename T>
class vector : public std::vector<T, Allocator<T>> {
 public:
  explicit vector(Arena& a) noexcept;
  explicit vector(Arena* a) noexcept;
  vector(Arena& a, size_t n);
  vector(Arena& a, size_t n, const T& v);
  template <typename It> vector(Arena& a, It first, It last);
};
```

`push_back` / `resize` / `operator[]` / range-for 全部继承自 `std::vector`，
只是构造时**必须**给 arena：

```cpp
shm::vector<Point> pts{arena};
pts.push_back({1.0f, 2.0f});
```

`shm::string` 同理。libstdc++ 的 SSO 缓冲指向对象自身，同地址映射下跨进程依然有效。

### 6.3 内存归谁管

- 消息对象 `T` 本身由 arena 分配
- `T` 内部的 `shm::vector` 缓冲也由同一个 arena 分配
- 因此整块内存只需要 `arena->Reset()` 就能归还，无需遍历

### 6.4 类型约束

```cpp
template <typename T> struct IsShmCompatible;              // 默认要求平凡析构
template <typename T> struct IsShmCompatible<vector<T>>;   // 递归
template <>           struct IsShmCompatible<string>;      // true

#define PANGOFLY_SHM_MESSAGE(...)  // 为自定义类型显式声明
```

消息对象永不析构，因此真正的要求是「析构无事可做」。这一条无法对任意用户
类型自动判定，于是提供 `PANGOFLY_SHM_MESSAGE` 作为显式契约；IDL 生成的类型
会自动带上它。

## 7. 消息层：块头状态机

```cpp
struct alignas(64) BlockHeader {
  uint64_t magic; uint32_t version; uint32_t index;
  std::atomic<uint32_t> lock;          // 跨进程自旋锁
  std::atomic<uint32_t> state;         // FREE / WRITING / PUBLISHED
  uint32_t generation;                 // 每次复用 +1（仅在 lock 内读写）
  std::atomic<uint32_t> readers;       // 活跃读者数（写端无锁扫描）
  uint32_t capacity; uint32_t used;
  uint32_t arena_offset;               // arena 相对 payload 的偏移
  uint32_t msg_offset;                 // ★ T 对象相对 payload 的偏移
  std::atomic<uint64_t> seq;           // 全局单调序号（写端无锁扫描）
  uint64_t timestamp_ns;
};
static_assert(sizeof(BlockHeader) == 64);
```

```
FREE --CAS--> WRITING --publish--> PUBLISHED --writer reclaim--> WRITING
  ^                |
  +---- abort -----+
```

- 所有迁移都在块内 `lock` 下完成，避免「读者刚拿到、写者就复用」。
- `seq` 由 `SegmentHeader::next_seq` 全局单调分发，读端以 `seq > since_seq` 过滤，天然去重且不丢消息。
- 无空闲块时，写端可回收「最旧且 `readers == 0`」的已发布块（latest-wins），
  全部在写或被读才返回背压。

**回收权只归写端。** `ReleaseRead` 仅递减 `readers`，绝不动 arena、绝不改 `state`。
早期版本让最后一个读者直接把块 `Reset + FREE`，这会与写端的回收扫描形成
「读者归还 → 立即 Reset → 写端同时复用」的三方踩踏；现在 `PUBLISHED` 块的
唯一出口是写端在 `TryAcquireWritable` 里把它转成 `WRITING`。

`readers` 与 `seq` 是 `std::atomic`：`TryAcquireWritable` 的 victim 扫描没有拿锁，
必须无锁读这两个字段，用普通 `uint32_t` 在 C++ 内存模型下属于数据竞争。

### 7.1 消息对象的位置

`T` 不是构造在 `BlockHeader` 上（那会覆盖块头），而是由 arena 分配：

```cpp
void* slot = arena->Allocate(sizeof(T), alignof(T));
T* msg = new (slot) T(*arena);
b->msg_offset = (uint8_t*)slot - (uint8_t*)payload;
```

读端用 `seg->MessageOf<T>(b)` 取回。这样块头与消息体互不覆盖，且两者共享同一个 arena。

## 8. 传输层：Segment

### 8.1 段布局

```
SegmentHeader（含 self_base 地址握手、next_seq、payload_base）
BlockHeader[0..n-1]（各一 cache line）
chunk 0: Arena 元数据 + 数据区
chunk 1: ...
```

### 8.2 关键接口

```cpp
bool OpenOrCreate(channel, cfg);      // 创建或打开，地址不一致即失败
BlockHeader* TryAcquireWritable();    // FREE -> WRITING，arena 已 Reset
void Publish(b, msg_offset, ts);      // WRITING -> PUBLISHED，分配 seq
void AbortWrite(b);                   // WRITING -> FREE
BlockHeader* AcquireRead(&since_seq); // 取 seq 最小且 > since_seq 的
void ReleaseRead(b);                  // 仅 readers--
BlockHeader* WaitRead(&since_seq, timeout_ns);

void*      BlockPayload(b);
Arena*     BlockArena(b);
template <typename T> T* MessageOf(b);
```

打开者以共享内存对象的实际大小（`fstat`）为准，不校验调用方传入的
`block_num` / `block_capacity`：读端通常只知道 channel 名，要求它重复声明
写端配置会让 attach 变得不必要地脆弱。创建者则把 `magic` 放在所有块头和
arena 初始化完成之后最后写入，作为「初始化完成」标志——否则另一个进程可能
在 `memset` 之后、arena 就绪之前就看到了合法 magic。

### 8.3 Sample / View 跨线程

`Sample<T>` 与 `View<T>` 都持有 `shared_ptr<Segment>`，因此它们可以脱离
`Writer` / `Reader` 句柄独立存活。典型场景是「线程 A 只 `Loan`，线程 B 填充并
`Publish`」：

```cpp
// 线程 A
auto s = writer->Loan();
queue.Push(std::move(s));        // move-only，只转移指针

// 线程 B
auto s = queue.Pop();
s->pixels.resize(bytes);         // 在共享内存里原地写
writer->Publish(std::move(s));
```

跨线程传递的成本是一个指针：消息对象始终留在共享内存中，不复制任何数据。
代价是块在 `WRITING` 状态停留更久、更容易触发背压，因此单线程场景应优先用
`writer->Write(fn)`。

## 9. 接口层

```cpp
auto s = w->Loan();               // 借块 + 就地构造 T
s->points.push_back(...);         // 直接写共享内存
w->Publish(std::move(s));         // 发布

auto v = r->Wait(timeout_ns);     // 取一条，readers++
use(v->points);                   // RAII 视图，析构自动归还
```

`Sample<T>` 是写端借据：未 `Publish` 就析构会自动 `Abort`，不会泄漏块。
`View<T>` 是读端视图：构造 `readers++`，析构 `readers--`，归零归还块。

`Node` 按 channel 在进程内复用同一个 `Segment`，保证同一进程里多个
writer/reader 映射到同一地址。

## 10. 验证结果

跨进程实测（640x480 RGB888，921600 字节/帧）：

```
[writer] published 1 msg_addr=0x600000000af0
...
[reader] seq=1 id=1 pts=50 msg_addr=0x600000000af0 ok=1
...
```

两端消息指针完全一致（`0x6000000009f0` / `0x600000000af0`），
`shm::vector<uint8_t>`、`shm::vector<Point>`、`shm::string` 全部可直接读取。

单进程三阶段测试（写满 → 读回 → 再写）与 ASan/UBSan 均通过。

## 11. A210 待验证项

- RISC-V 64 位上 `0x600000000000` 起始的 VA 窗口是否可用（需确认内核 VA 布局）
- GCC 8.3 + uclibc 下 `std::vector` 的 allocator 传播行为
- 与 TORQ 运行时共存时的内存映射冲突

## 12. 迁移历史

v2 一次性替换掉了旧链路上的全部组件，未保留兼容层：

- `core/memory/*`（pmr 注入）
- `core/transport/shm/*`（旧 segment / posix_segment / k230_shm / shm_conf / state / shm_region）
- `core/node/*`（旧 Sample / Reader / Writer / Node）
- `core/message/message_info.h`
- `idl/allocator/*`、`idl/container/*`、`idl/generated/*`、`idl/idl_generator.py`
- `rtos_k230/`、`gesture/`、`test/test_nonpod_x86.cc`
- 依赖旧 API 的示例（`examples/core/*`、`examples/luckfox/*`、`examples/nonpod_*`）
