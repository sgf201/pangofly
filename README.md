# pangofly

**pango** - 大块头，代表强大的硬件计算能力和稳定的基础设施
**fly** - 飞起来，代表高效、敏捷的实时通信能力

A hardware-optimized, efficient, memory-safe, inter-process communication component designed for autonomous driving.

## 项目概述

pangofly 是一个面向自动驾驶场景的进程间通信（IPC）框架，基于共享内存 + 固定虚拟地址映射实现零拷贝通信。

## 设计要点（v2）

三条硬需求驱动了 v2 的重构：

1. **零拷贝**：发送端直接在共享内存中构造消息对象，接收端直接使用该对象。
2. **同地址映射**：收发双方把同一段共享内存映射到完全相同的虚拟地址，因此消息内的裸指针、容器指针天然跨进程有效。
3. **支持 non-POD**：消息可包含 `shm::vector` / `shm::string` 等变长成员，编程方式与标准容器一致。

### 分层

| 层 | 文件 | 职责 |
|----|------|------|
| 地址层 | [region.h](core/shm/region.h) / [region.cc](core/shm/region.cc) | 全局窗口注册表，为每个 channel 分配独占且固定的 VA 窗口；映射失败即硬失败，绝不回退 |
| 分配层 | [arena.h](core/shm/arena.h) | 块内 TLSF 分配器，O(1) 分配/释放，O(1) `Reset()` 整体回收 |
| 类型层 | [allocator.h](core/shm/allocator.h) | `shm::Allocator`（默认构造被 `delete`）+ `shm::vector` / `shm::string` |
| 消息层 | [block.h](core/shm/block.h) / [message.h](core/shm/message.h) | 块头状态机 + `Sample<T>`（写端借据）/ `View<T>`（读端视图） |
| 传输层 | [segment.h](core/shm/segment.h) / [segment.cc](core/shm/segment.cc) | channel 的段管理、发布/获取、地址握手 |
| 接口层 | [node.h](core/shm/node.h) / [pangofly.h](core/pangofly.h) | `Writer<T>` / `Reader<T>` / `Node` |

### 与旧实现的关键差异

| 旧（pmr） | 新（v2） |
|-----------|----------|
| `std::pmr` + thread_local `ScopedAllocator` 隐式注入 | `shm::Allocator` 显式持有 `Arena*`，默认构造 `= delete` |
| `polymorphic_allocator` 必须可默认构造 → 忘记绑定即静默落私有堆 | 忘记传 arena 是**编译错误** |
| 单调池 `do_deallocate` 是 no-op，`vector` 语义不完整 | TLSF 真正释放，`vector` 语义完整 |
| `channel_id % 100 * 2MB` 哈希取模，必然重叠 | 注册表完整记录名字，每 channel 独占窗口 |
| `mmap(nullptr, ...)` 静默回退 → 指针跨进程失效 | `MAP_FIXED_NOREPLACE` 探测 + `self_base` 地址握手，失败即硬失败 |
| 块头只有 `msg_size_` / `sequence_`，写读无法区分 | 四态 + generation + readers refcount |
| `CalculateSize()` 只算一层 | 递归 `DynamicSize()` |

## 快速开始

```cpp
#include "core/pangofly.h"

struct Point { float x, y; };

struct Frame {
  int64_t stamp_ns;
  ::pangofly::shm::vector<Point> points;

  explicit Frame(::pangofly::shm::Arena& arena) : stamp_ns(0), points(arena) {}
};
PANGOFLY_SHM_MESSAGE(Frame)

int main() {
  pangofly::Init();
  auto node = pangofly::CreateNode("demo");

  // 写端：直接在共享内存里构造
  auto w = node->CreateWriter<Frame>("my_channel");
  auto s = w->Loan();
  s->stamp_ns = 123;
  for (int i = 0; i < 100; ++i) s->points.push_back({float(i), float(i)});
  w->Publish(std::move(s));

  // 读端：直接使用，无拷贝
  auto r = node->CreateReader<Frame>("my_channel");
  auto v = r->Acquire();
  if (v.valid()) {
    // v->points 就是共享内存里的那个 vector
  }
}
```

消息类型需满足「析构无事可做」：只含标量、`shm::vector`、`shm::string` 或递归满足该条件的结构体。
块回收不做逐元素析构，而是 `arena->Reset()`。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/examples/common/shm_example/shm_example
```

跨进程验证：

```bash
./build/examples/common/shm_example/shm_reader 10 4000 &
./build/examples/common/shm_example/shm_writer 10 50
```

## IDL

`idc` 会为每个 struct 生成 `explicit X(shm::Arena&)` 构造与递归 `DynamicSize()`：

```bash
./build/idc -o yolo_detection.h examples/yolov5/idl/yolo_detection.idl
```

## 约束

- 消息对象永不析构，整块内存由 `Arena::Reset()` 一次性回收。
- 消息内不得出现 `std::vector` / `std::string` / 裸指针 / 智能指针等持有堆资源的成员。
- 固定地址要求目标平台允许在指定 VA 处 `mmap`；失败会明确报错，不会静默降级。
