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

注册表放在独立的小 shm（`/pangofly_v2_registry`），固定映射在 `RegionConfig::registry_base`，
与数据窗口区互不干扰。

`RegionConfig` 的默认值按指针宽度分两套——48 位地址在 32 位平台会被截断成 0，
若沿用 64 位的默认值，`pangofly::Init()` 在 armv7l 上必然失败（旧版本需要应用层
自己写一份 `RegionConfig` 才能启动）：

| 平台 | base | total_size | window_size | max_channels | registry_base |
|------|------|-----------|-------------|--------------|---------------|
| 64 位 | `0x600000000000` | 8 GiB | 64 MiB | 128 | `0x580000000000` |
| 32 位 | `0x40000000` | 512 MiB | 64 MiB | 8 | `0x70000000` |

32 位窗口区结束于 `0x60000000`，注册表在窗口区之外，不会冲突。

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

### 4.2.1 跨进程互斥：为什么用 flock 而不是原子变量

注册表的槽位分配不是单次 CAS，而是「占首槽 → 逐个标记延续槽 → 写名字/大小」
的多步写。中间状态对其他进程可见，必须串行化，否则两个进程可能同时看到
同一段连续空槽并重复分配。

这里用 **flock 文件锁**（`core/shm/file_lock.{h,cc}`），而不是共享内存里的
原子变量：

| | 原子变量 | flock |
|---|---|---|
| 持有者崩溃 | 锁值永久保留，需额外判活才敢接管 | 内核自动释放 |
| 语义 | 自旋等待，空转 CPU | 阻塞睡眠，被唤醒即拿到 |
| 判断正确性 | 依赖「进程是否存活」推断，有误判风险 | 内核保证，无需推断 |

锁文件路径 `FileLockPath("pangofly_v2_registry.lock")`：优先 `/dev/shm`，
不可写时退回 `/tmp`。锁文件本身不删除——删除会与正在等待的进程产生竞态
（等待者持有的是已 unlink 的 inode，新来者创建新文件，两边不再互斥）。

加锁范围：

| 操作 | 是否加锁 | 原因 |
|---|---|---|
| `Region::Init` 的建表段 | 是 | 两个进程同时启动时，A 可能刚 `shm_open` 完（size=0、magic 未写），B 就 mmap 后按未初始化内容判断 |
| `Region::Acquire` | 是 | 多步槽位分配 |
| `Region::Release` | 是 | 多步槽位清理 |

进程内另有一把 `std::mutex`，两层叠加：`mutex` 管同进程多线程，`flock` 管跨进程。

验证：6 个进程并发 `Acquire` 不同 channel，20 轮压测，每轮 6 个窗口地址
互不重叠（`scripts/p0_check.sh` 第 6 项 + 独立压测脚本）。

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
  std::atomic<uint64_t> lock;          // 0 或持有者 token（pid<<32 | start）
  std::atomic<uint32_t> state;         // FREE / WRITING / PUBLISHED
  std::atomic<uint32_t> readers;       // 活跃读者数（写端无锁扫描）
  uint32_t generation;                 // 每次复用 +1（仅在 lock 内读写）
  uint32_t msg_offset;                 // ★ T 对象相对 payload 的偏移
  uint32_t owner_pid;                  // ★ WRITING 持有者（崩溃回收用）
  uint32_t owner_start;                // ★ 持有者的进程启动时刻
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

### 7.1 崩溃恢复

写端在 `WRITING` 状态崩溃（`kill -9`、段错误、掉电重启后的残留）时，那一块既
不会被 `Publish` 也不会被 `AbortWrite`，`state` 永远停在 `WRITING`，块内自旋锁
也永远不释放。块数少的通道几次崩溃后就彻底写不进去。

恢复依赖两个字段记录持有者身份：

```cpp
uint32_t owner_pid;      // 拿到 WRITING 的进程
uint32_t owner_start;    // 该进程的启动时刻（/proc/<pid>/stat 第 22 字段）
```

**为什么必须带 starttime。** 只记 pid 不够——pid 会被内核复用。若写端崩溃后
另一个进程恰好拿到同一 pid，仅凭 pid 会把它误判成「写端还活着」，块依旧卡死；
反过来也可能把还在跑的新进程误判成死的而抢走它正在写的块。pid + starttime
组合才唯一标识一个进程实例。

三条路径都会用到它：

1. `BlockHeader::Lock()` 抢锁失败时读 token 里的持有者，判定已退出就 CAS 接管，
   不会永久自旋。
2. `Segment::ReclaimZombieWriting()` 扫描所有 `WRITING` 块，持有者已退出的
   一并 `Reset` 成 `FREE`。
3. `TryAcquireWritable()` 在「无 FREE 块」后先尝试一次僵尸回收，再考虑回收
   已发布块。

`ProcessToken` 中 `start == 0` 表示该 token 无法比对（`/proc` 不可用），此时
`IsProcessAlive` 保守返回 `true`：宁可等写端下次正常 `Abort`，也不能误抢一个
还活着的写端。`Publish` 成功时会把 `owner_pid/owner_start` 清零——已发布块的
回收只看 `readers` 与 `seq`，与原写端进程是否存活无关。

代价：一次判定要读一次 `/proc/<pid>/stat`。健康路径上只有「抢不到锁」和
「通道无 FREE 块」才会走到这里，正常发布流程完全不碰。

### 7.2 消息对象的位置

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
SegmentHeader（含 self_base 地址握手、next_seq、payload_base、type_hash）
BlockHeader[0..n-1]（各一 cache line）
chunk 0: Arena 元数据 + 数据区
chunk 1: ...
```

### 8.2 类型校验

`channel` 只是 shm 对象名，本身不携带任何类型信息。两端若声明了不同的消息
类型却共用同名 channel，读端会按错误的布局解释共享内存：轻则读到垃圾，重则
越过 arena 边界访问。

创建段时把消息类型身份写进段头：

```cpp
uint32_t type_hash;         // 类型名的 32 位 FNV-1a
uint32_t type_name_len;
char     type_name[64];     // 截断存储，仅供报错输出
```

类型名由 `Node::CreateWriter<T>` / `CreateReader<T>` 自动传入：优先取
`T::GetTypeName()`（IDL 生成的类型都有），手写类型没有时退化为编译期的
`__PRETTY_FUNCTION__` 签名，不依赖 RTTI。

校验发生在两处：

- `Segment::OpenOrCreate`：attach 时比对，不一致直接失败，并打印两端类型名
- `Node::GetSegment`：命中进程内已缓存的 Segment 时同样比对，拦住
  「同一进程内 `CreateWriter<A>` 与 `CreateReader<B>` 共用 channel」这种
  最容易漏掉的写法

指纹只用于一致性判断，不承担安全职责；32 位哈希碰撞的概率远低于人为写错
类型的概率，且段头同时存了截断的类型名可直接定位。

### 8.3 关键接口

```cpp
bool OpenOrCreate(channel, cfg, type_name);  // 创建或打开，地址/类型不一致即失败
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
`View<T>` 是读端视图：构造/拷贝 `readers++`，析构 `readers--`。

`Node` 按 channel 在进程内复用同一个 `Segment`，保证同一进程里多个
writer/reader 映射到同一地址。

### 9.1 对外不暴露裸指针

上层只能拿到 `Sample<T>` / `View<T>` 两个 RAII 句柄，拿不到 `BlockHeader*`：

| 原先暴露 | 现在 | 原因 |
|---|---|---|
| `Sample::get()` | 删除，用 `*s` / `s->` | 裸指针可被存到作用域外 |
| `View::get()` | 删除，用 `*v` / `v->` | 同上 |
| `Sample::block()` | 改私有，`friend Writer<T>` | 块句柄是内部实现 |
| `Sample::segment()` | 删除 | 上层不该操作 Segment |
| `Sample::arena()` | 删除 | 直接用 `s->field` 即可 |
| `Segment::TryAcquireWritable()` 等 | 改私有，`friend Writer/Reader` | 块级接口仅供消息层 |
| `reader->Observe(fn)` 传 `T&` | 改传 `const View<T>&` | 引用在回调结束后悬空 |

需要打印地址时用 `Address()`，返回 `uintptr_t` 而非指针——无法被解引用，
也就不会被误存成悬空指针。

`View<T>` 因此改为**可拷贝**：拷贝即共享同一块，`readers` 再加一，
保护期延长到最后一个副本析构，且不产生任何堆分配（复用块头里的计数器）。

```cpp
auto v = reader->Wait(100);
if (v.valid()) {
    std::vector<pangofly::shm::View<Msg>> held;
    held.push_back(v);          // 合法：拷贝延长保护期
    use(held[0]->points);       // v 析构后依然安全
}
```

代价：副本被长期持有时该块无法被淘汰或回收，写端可能因此背压。这与
`shared_ptr` 的行为一致，属于使用者需要理解的契约。

### 9.2 keep_last(history)

`SegmentConfig::history` 控制收端最多能保留多少条未消费消息：

```cpp
pangofly::shm::SegmentConfig cfg;
cfg.block_num = 6;
cfg.history = 2;                 // 收端最多看到最新 2 条
auto w = node->CreateWriter<Msg>("ch", cfg);
```

| 取值 | 行为 |
|------|------|
| `0`（默认） | 不限。写端只在借不到空块时才回收最旧块 |
| `N > 0` | 已发布块超过 N 时，写端在 `Publish` 里立刻把最旧的降级为 `FREE` |

淘汰发生在 `Publish` 内部而不是借块时，这样「收端最多保留 N 条」是与写端
节奏无关的确定性约束：连续发 5 条、`history=2`，收端只会读到第 4、5 条。

细节：

- 生效值被 clamp 到 `[1, block_num]`，写入 `SegmentHeader::history`，
  打开者从段头读取，不需要重复声明配置
- 淘汰时若最旧块 `readers != 0`（正被读者持有），本次跳过，下一次
  `Publish` 会补上；这期间可能短暂超过 N 条
- `history` 只影响「保留多少条」，不影响块总数。设 `history < block_num`
  可以让写端始终有富余块可借，避免频繁走 victim 回收路径
- 代价与 latest-wins 相同：被淘汰的消息直接 Reset，收端再也读不到

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

单进程四阶段测试（写满 → 读回 → 再写 → keep_last）与 ASan/UBSan 均通过：

```
stage1 extra Loan valid=1 (expect 1: recycle oldest)
stage2 read 4 messages, content_ok=1
stage3 loaned=4 (expect 4)
stage4 kept=2 first_ts=4 (expect 2 / 4) -> PASS
```

## 11. 已知限制

### 11.1 通知机制：futex（已实现）

早期版本 `WaitRead` 是固定 100us 轮询，`Publish` 不唤醒任何人。现已改为
**段头 futex word + 跨进程 FUTEX_WAKE**。

`SegmentHeader` 新增 `notify_seq`（`atomic<uint32_t>`，语义为「已发布消息条数」）：

```cpp
// Publish：state 置 PUBLISHED 之后
header_->notify_seq.fetch_add(1, std::memory_order_release);
FutexWake(&header_->notify_seq, INT_MAX);

// WaitRead：取不到才进等待
BlockHeader* b = AcquireRead(since_seq);
if (b) return b;
const uint32_t observed = header_->notify_seq.load(std::memory_order_acquire);
b = AcquireRead(since_seq);            // 再查一次，避免 lost wakeup
if (b) return b;
FutexWait(&header_->notify_seq, observed, &ts);   // 超时兜底
```

三个关键点：

1. **必须用跨进程 futex**（`SYS_futex`，不加 `FUTEX_PRIVATE_FLAG`）——段是
   `MAP_SHARED` 映射到同地址，futex word 天然跨进程共用。
2. **`FUTEX_WAIT` 自带值比较**：若在「读 seq」与「进 wait」之间写端已 `fetch_add`，
   内核发现值不匹配会立刻返回 `EAGAIN`，不会睡死。
3. **取消息 → 读 seq → 再取消息 → wait** 的顺序不能省第二步的复查，否则仍有
   一个极窄的丢唤醒窗口。

无 `SYS_futex` 的平台（部分 uclibc / 老内核头）自动退化为 ≤5ms 的短睡轮询，
语义不变。

效果（x86 Debug 构建）：

| 场景 | 结果 |
|---|---|
| 读端 `Wait(2s)`，期间无消息 | 总 CPU 时间 `user 0.00s / sys 0.00s`（旧实现会累积约 2 万次 `nanosleep`） |
| 写端延迟 300ms 后发布一帧 | 读端在该帧发布后立即返回，总耗时 401ms ≈ 300ms 发送延时 + 启动开销 |
| 唤醒正确性 | 读到 `payload=2048`，内容完整 |

验证方式见 `scripts/p0_check.sh` 第 5 项。

### 11.2 其余可选方向

| 方案 | 延迟 | 复杂度 | 崩溃安全 | 现状 |
|------|------|--------|----------|------|
| 段头 futex word | ~1us | 中 | 安全 | **已实现** |
| 环形通知队列（多槽，防伪共享） | ~1us | 高 | 安全 | 未做：需要多读者场景才有收益 |
| shm 内 `pthread_cond_t` | ~1us | 中高 | 需 robust mutex，进程崩溃会 hang | 不采用 |

futex 当前是「单个 word + 全量唤醒」，所有等待者被一起唤醒后各自
`AcquireRead`。读者数少（个位数）时这比环形队列更简单且没有丢失风险；
若将来出现「单个 channel 数十个读者」的场景，再考虑 scyber 那种
512 槽环形缓冲 + 转置 seqs。

### 11.3 已修复：崩溃恢复与类型校验

早期版本存在两个运行期缺口，现已补齐（见 §7.1、§8.2）：

| 缺口 | 后果 | 现状 |
|------|------|------|
| 写端在 WRITING 崩溃 | 块永久卡死，自旋锁永不解 | 已修复：owner token + 主动回收 |
| 无消息类型校验 | 按错误布局读 shm，读垃圾或越界 | 已修复：段头 type_hash 双侧校验 |

### 11.4 已修复：重复 Reader

同一 Node 内对同一 channel 重复 `CreateReader` 曾会静默成功。两个 Reader
各自维护 `since_seq_`，写端回收块时无法同时满足两个进度，表现为其中一方
「随机丢帧」。

现改为在 `Node::CreateReader` 里用 `std::map<std::string, std::weak_ptr<void>>`
查重：已有存活 Reader 则返回 `nullptr` 并打 `[ERROR]`；旧 Reader 已销毁则
允许重建。用 `weak_ptr` 而非 `shared_ptr` 是为了不让 Node 延长 Reader 的
生命周期——Reader 仍由调用方持有。

需要多处消费同一 channel 时，应共享同一个 Reader，或放到不同 Node 里。

### 11.5 验证矩阵

`scripts/p0_check.sh` 覆盖全部修复项：

```
1. locktakeover        死进程持有的锁可被接管（~190us，非超时）
2. type_a / type_b     跨进程类型不匹配被拒绝，打印两端类型名
   type_inproc         同进程缓存复用同样被拒绝
3. hold + kill -9      僵尸块被回收，同通道继续写 6 帧（块数 3）
4. hold×3 + 存活       写端正确背压，不误抢在写块
   hold×3 + kill -9    3 个僵尸块全部回收，写入立即恢复
5. waitwake/wakesend   futex 唤醒：读端 2s 空闲 CPU 为 0；写端发布后立即返回
6. slot × 6 并发       6 个进程申请的窗口地址互不重叠（flock 互斥）
7. dupreader           重复 Reader 被拒绝，释放后可重建
```

未覆盖的场景：若进程在持有 WRITING 的同时被 `SIGSTOP` 挂起（而非退出），
它仍被判为存活，块会一直等到该进程被 `SIGCONT` 或 `SIGKILL`。这是有意为之——
无法区分「挂起」与「慢」，贸然抢占会破坏正在写的数据。

## 12. A210 待验证项

- RISC-V 64 位上 `0x600000000000` 起始的 VA 窗口是否可用（需确认内核 VA 布局）
- GCC 8.3 + uclibc 下 `std::vector` 的 allocator 传播行为
- 与 TORQ 运行时共存时的内存映射冲突
- ~~无通知机制下的实际唤醒延迟，是否满足上层帧率~~ 已完成 futex 改造，x86 上验证通过；A210 上待实测

## 13. 迁移历史

v2 一次性替换掉了旧链路上的全部组件，未保留兼容层：

- `core/memory/*`（pmr 注入）
- `core/transport/shm/*`（旧 segment / posix_segment / k230_shm / shm_conf / state / shm_region）
- `core/node/*`（旧 Sample / Reader / Writer / Node）
- `core/message/message_info.h`
- `idl/allocator/*`、`idl/container/*`、`idl/generated/*`、`idl/idl_generator.py`
- `rtos_k230/`、`gesture/`、`test/test_nonpod_x86.cc`
- 依赖旧 API 的示例（`examples/core/*`、`examples/luckfox/*`、`examples/nonpod_*`）
