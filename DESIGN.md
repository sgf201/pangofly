# pangofly 设计文档

## 1. 项目概述

### 1.1 项目定位
pangofly 是一个专为自动驾驶场景设计的高性能进程间通信（IPC）框架，采用共享内存技术实现高效、低延迟的数据传输。

### 1.2 名称含义
- **pango** - 大块头，代表强大的硬件计算能力和稳定的基础设施
- **fly** - 飞起来，代表高效、敏捷的实时通信能力

### 1.3 核心特性
- 基于共享内存的零拷贝通信
- 同地址映射技术确保多进程地址一致性
- 线程安全的数据读写
- 支持订阅/发布模式
- 专为自动驾驶实时场景优化

---

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        pangofly 框架                           │
├─────────────────────────────────────────────────────────────────┤
│                        应用层 (Application)                     │
│    ┌──────────────┐          ┌──────────────┐                  │
│    │   Node A     │          │   Node B     │                  │
│    │ (Publisher)  │          │ (Subscriber) │                  │
│    └──────┬───────┘          └──────┬───────┘                  │
│           │                         │                          │
│           │ Writer                  │ Reader                   │
│           ▼                         ▼                          │
├─────────────────────────────────────────────────────────────────┤
│                        节点层 (Node Layer)                      │
│    ┌─────────────────────────────────────────────────┐         │
│    │  Node | Reader | Writer | ReaderBase | WriterBase│         │
│    └─────────────────────────────────────────────────┘         │
│                              │                                 │
│                              ▼                                 │
├─────────────────────────────────────────────────────────────────┤
│                        传输层 (Transport Layer)                 │
│    ┌─────────────────────────────────────────────────┐         │
│    │  Segment | PosixSegment | ShmConf | State | Block│         │
│    └─────────────────────────────────────────────────┘         │
│                              │                                 │
│                              ▼                                 │
├─────────────────────────────────────────────────────────────────┤
│                        共享内存层 (SHM)                         │
│              ┌─────────────────────────┐                       │
│              │  /dev/shm/pangofly_xxx  │                       │
│              └─────────────────────────┘                       │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 模块划分

| 模块 | 目录 | 职责 |
|------|------|------|
| **common** | `pangofly/common/` | 通用工具、日志、类型定义 |
| **message** | `pangofly/message/` | 消息结构定义 |
| **node** | `pangofly/node/` | 节点、读写器抽象 |
| **transport** | `pangofly/transport/shm/` | 共享内存传输实现 |
| **examples** | `pangofly/examples/` | 使用示例 |

---

## 3. 核心组件设计

### 3.1 Node（节点）

**职责**：作为通信的核心节点，管理 Reader 和 Writer 的生命周期。

**类结构**：
```cpp
class Node {
public:
  const std::string& Name() const;
  
  template <typename MessageT>
  std::shared_ptr<Writer<MessageT>> CreateWriter(const std::string& channel_name);
  
  template <typename MessageT>
  std::shared_ptr<Reader<MessageT>> CreateReader(const std::string& channel_name, 
                                                  const CallbackFunc<MessageT>& callback);
  // ...
};
```

**文件位置**：[node.h](file:///home/sgf/ws/pangofly/pangofly/node/node.h)

### 3.2 Writer（写入器）

**职责**：向共享内存写入消息。

**接口定义**：
```cpp
template <typename MessageT>
class Writer {
public:
  virtual bool Write(const MessageT& message) = 0;
  virtual bool Write(const MessageT& message, const MessageInfo& message_info) = 0;
  virtual const std::string& ChannelName() const = 0;
};
```

**文件位置**：[writer.h](file:///home/sgf/ws/pangofly/pangofly/node/writer.h)

### 3.3 Reader（读取器）

**职责**：从共享内存读取消息，支持回调机制。

**接口定义**：
```cpp
template <typename MessageT>
class Reader {
public:
  virtual bool Init() = 0;
  virtual void SetCallback(const CallbackFunc<MessageT>& callback) = 0;
  virtual bool Read(MessageT* message, MessageInfo* message_info) = 0;
  virtual const std::string& ChannelName() const = 0;
};
```

**文件位置**：[reader.h](file:///home/sgf/ws/pangofly/pangofly/node/reader.h)

### 3.4 Segment（共享内存段）

**职责**：管理共享内存的分配、映射和释放。

**核心方法**：
- `AcquireBlockToWrite()` - 获取可写块
- `ReleaseWrittenBlock()` - 释放已写块
- `AcquireBlockToRead()` - 获取可读块
- `ReleaseReadBlock()` - 释放已读块

**文件位置**：[segment.h](file:///home/sgf/ws/pangofly/pangofly/transport/shm/segment.h)

### 3.5 PosixSegment（POSIX共享内存段）

**职责**：基于 POSIX 共享内存实现，核心是同地址映射技术。

**关键设计**：
```cpp
class PosixSegment : public Segment {
private:
  uint64_t fixed_address_ = 0x7f0000000000ULL;  // 固定映射地址
};
```

**文件位置**：[posix_segment.h](file:///home/sgf/ws/pangofly/pangofly/transport/shm/posix_segment.h)

### 3.6 ShmConf（共享内存配置）

**职责**：计算共享内存大小、块数量等配置参数。

**文件位置**：[shm_conf.h](file:///home/sgf/ws/pangofly/pangofly/transport/shm/shm_conf.h)

---

## 4. 关键技术：同地址映射

### 4.1 技术原理

同地址映射是 pangofly 的核心技术，通过 `mmap` 的 `MAP_FIXED` 标志将共享内存强制映射到相同的虚拟地址。

**核心代码**（posix_segment.cc）：
```cpp
void* desired_addr = reinterpret_cast<void*>(fixed_address_);
managed_shm_ = mmap(desired_addr, conf_.managed_shm_size(), 
                    PROT_READ | PROT_WRITE, 
                    MAP_SHARED | MAP_FIXED, 
                    fd, 0);
```

### 4.2 技术优势

| 优势 | 说明 |
|------|------|
| **零拷贝** | 多个进程直接访问同一地址，无需数据拷贝 |
| **指针传递** | 可以直接传递指针，无需序列化 |
| **高性能** | 避免地址转换开销，提升通信效率 |
| **内存安全** | 固定地址避免地址随机化带来的问题 |

### 4.3 地址布局

```
虚拟地址空间布局：
┌──────────────────────────────────────┐ 0x7fffffffffff
│              栈区 (Stack)            │
├──────────────────────────────────────┤
│              堆区 (Heap)             │
├──────────────────────────────────────┤
│    共享内存段 (Fixed Address)        │ 0x7f0000000000
│    ┌────────────────────────────┐    │
│    │ State (状态信息)           │    │
│    │ Blocks (数据块数组)        │    │
│    │ Block Buffers (数据缓冲区) │    │
│    └────────────────────────────┘    │
├──────────────────────────────────────┤
│            共享库 (Libraries)        │
├──────────────────────────────────────┤ 0x000000000000
```

---

## 5. 数据结构设计

### 5.1 State（共享内存状态）

```cpp
struct State {
  std::atomic<uint64_t> seq;           // 序列号
  std::atomic<uint16_t> w_index;       // 写索引
  std::atomic<uint16_t> r_index;       // 读索引
  uint32_t block_num;                  // 块数量
  uint32_t block_buf_size;             // 块大小
};
```

### 5.2 Block（数据块）

```cpp
struct Block {
  std::atomic<uint64_t> seq;           // 消息序列号
  std::atomic<bool> is_writing;        // 写入状态
  uint32_t msg_size;                   // 消息大小
  uint8_t* buf;                        // 数据缓冲区
};
```

### 5.3 MessageInfo（消息元信息）

```cpp
struct MessageInfo {
  uint64_t seq;                        // 序列号
  uint64_t timestamp;                  // 时间戳
  uint32_t msg_size;                   // 消息大小
};
```

---

## 6. 通信流程

### 6.1 写入流程

```
Writer.Write(message)
       │
       ▼
Segment.AcquireBlockToWrite(msg_size)
       │
       ├─→ 获取可用 Block
       ├─→ 标记 is_writing = true
       └─→ 返回 WritableBlock
       │
       ▼
memcpy(block.buf, message, msg_size)
       │
       ▼
Segment.ReleaseWrittenBlock()
       │
       ├─→ 更新 seq
       ├─→ 更新 w_index
       └─→ 标记 is_writing = false
```

### 6.2 读取流程

```
Reader.Read(message, info)
       │
       ▼
Segment.AcquireBlockToRead()
       │
       ├─→ 检查 r_index < w_index
       ├─→ 获取可读 Block
       └─→ 返回 ReadableBlock
       │
       ▼
memcpy(message, block.buf, block.msg_size)
       │
       ▼
Segment.ReleaseReadBlock()
       │
       └─→ 更新 r_index
```

---

## 7. 线程安全设计

### 7.1 同步机制

| 组件 | 同步方式 | 保护对象 |
|------|----------|----------|
| Node | `std::mutex` | readers_、writers_ 集合 |
| Segment | `std::recursive_mutex` | 共享内存操作 |
| State | `std::atomic<T>` | seq、w_index、r_index |
| Block | `std::atomic<bool>` | is_writing 标志 |

### 7.2 并发访问模式

```
                    ┌─────────────┐
                    │   State     │  ← atomic operations
                    └──────┬──────┘
                           │
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
   ┌─────────┐      ┌─────────┐      ┌─────────┐
   │  Block0 │      │  Block1 │      │  BlockN │  ← atomic is_writing
   └─────────┘      └─────────┘      └─────────┘
```

---

## 8. 编译与使用

### 8.1 依赖

- C++17 或更高版本
- POSIX 共享内存支持（Linux）
- CMake 3.10+

### 8.2 构建命令

```bash
mkdir build && cd build
cmake ..
make
```

### 8.3 基本使用示例

```cpp
#include "pangofly/pangofly.h"

struct MyMessage {
  int32_t id;
  float data[100];
};

// 发布者
void publisher() {
  pangofly::Init();
  auto node = pangofly::CreateNode("publisher_node");
  auto writer = node->CreateWriter<MyMessage>("channel");
  
  MyMessage msg{1, {1.0f, 2.0f, 3.0f}};
  writer->Write(msg);
  
  pangofly::Shutdown();
}

// 订阅者
void subscriber() {
  pangofly::Init();
  auto node = pangofly::CreateNode("subscriber_node");
  
  auto reader = node->CreateReader<MyMessage>("channel", 
    [](const MyMessage& msg, const MessageInfo& info) {
      std::cout << "Received: id=" << msg.id << std::endl;
    });
  
  node->Observe();
  pangofly::Shutdown();
}
```

---

## 9. 文件结构

```
pangofly/
├── CMakeLists.txt
├── pangofly/
│   ├── common/
│   │   ├── log.h
│   │   ├── macros.h
│   │   └── types.h
│   ├── message/
│   │   └── message_info.h
│   ├── node/
│   │   ├── node.h
│   │   ├── node.cc
│   │   ├── reader.h
│   │   ├── reader_base.h
│   │   ├── writer.h
│   │   └── writer_base.h
│   ├── transport/
│   │   └── shm/
│   │       ├── block.h
│   │       ├── posix_segment.h
│   │       ├── posix_segment.cc
│   │       ├── segment.h
│   │       ├── segment.cc
│   │       ├── shm_conf.h
│   │       ├── shm_conf.cc
│   │       ├── state.h
│   │       └── state.cc
│   ├── examples/
│   │   └── shm_example.cc
│   ├── pangofly.h
│   └── pangofly.cc
├── README.md
└── DESIGN.md
```

---

## 10. 设计原则

| 原则 | 说明 |
|------|------|
| **高性能** | 零拷贝、同地址映射、原子操作 |
| **内存安全** | 固定地址避免野指针、边界检查 |
| **可扩展性** | 模板化设计支持任意消息类型 |
| **线程安全** | 细粒度锁、原子变量 |
| **实时性** | 低延迟、无阻塞设计 |
| **可维护性** | 清晰的模块划分、接口抽象 |