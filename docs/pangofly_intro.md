# Pangofly：RT-Smart 下的高性能共享内存通信库

## 一、项目概述

**Pangofly** 是一个基于共享内存的高性能跨进程通信库，专为 K230 平台的 RT-Smart 实时操作系统设计。它提供了类似 ROS（Robot Operating System）的消息传递机制，支持 POD 和 Non-POD 类型的数据传输。

### 1.1 核心特性

| 特性 | 描述 |
|------|------|
| **高性能** | 基于共享内存，零拷贝通信 |
| **固定地址映射** | Writer/Reader 使用相同虚拟地址 |
| **Non-POD 支持** | 支持 Vector、String 等复杂类型 |
| **自动内存管理** | 基于块的分配器，自动回收 |
| **跨平台** | 支持 x86 和 RISC-V |

### 1.2 应用场景

- AI 推理结果传输
- 机器视觉数据共享
- 实时传感器数据流
- 进程间高速通信

---

## 二、核心实现原理

### 2.1 共享内存架构

```
┌─────────────────────────────────────────────────────────────┐
│                    虚拟内存布局                             │
├─────────────────────────────────────────────────────────────┤
│  0x000000000 - 0x100000000  (4GB)  │  系统内核空间        │
├─────────────────────────────────────────────────────────────┤
│  0x100000000 - 0x120000000  (0.5GB)│  用户空间            │
├─────────────────────────────────────────────────────────────┤
│  0x120000000 - 0x1A0000000  (256MB)│  Pangofly 预留区域   │
│                                   │  ← 固定地址映射      │
├─────────────────────────────────────────────────────────────┤
│  0x1A0000000 - 0x200000000  (1.5GB)│  用户空间            │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 基于块的内存分配器

Pangofly 采用类似 Cybert 的块分配策略：

```cpp
struct BlockHeader {
    uint64_t magic;           // 魔数验证
    uint32_t block_size;      // 块大小
    std::atomic<uint32_t> status;  // FREE/RESERVED/IN_USE
    BlockHeader* next;        // 双向链表
    BlockHeader* prev;
};

struct ChannelBlockPool {
    uint64_t magic;
    uint32_t num_blocks;      // 总块数
    uint32_t block_size;      // 每块大小（默认 64KB）
    std::atomic<BlockHeader*> free_list;
    std::atomic<uint32_t> free_count;
};
```

**工作流程：**
1. Writer 从 Pool 获取一个块
2. 使用自定义分配器在块内分配对象
3. 发送完成后直接归还整个块
4. Reader 读取后直接归还块

### 2.3 智能指针优化

为了避免逐个析构元素，使用 `BlockPtr` 智能指针：

```cpp
template<typename T>
class BlockPtr {
public:
    ~BlockPtr() {
        if (block_allocator_ && data_) {
            // 直接归还块，不调用析构函数
            block_allocator_->deallocate_block(data_);
        }
    }
};
```

**性能对比：**

| 操作 | 传统方式 | Pangofly |
|------|----------|----------|
| 析构 1000 元素 | 1000 次析构调用 | 直接归还块（O(1)） |

---

## 三、快速开始

### 3.1 环境准备

```bash
# 克隆仓库（包含 submodule）
git clone --recurse-submodules https://github.com/sgf201/k230_pangofly.git
cd k230_pangofly

# 安装依赖（Ubuntu）
sudo apt-get install build-essential cmake
```

### 3.2 编译选项配置

通过 `make menuconfig` 配置预留区域：

```
RT_USING_LWP  --->
  [*] Enable Pangofly shared memory reserved region
    (0x120000000) Start address
    (0x80000000) Size (256MB)
```

### 3.3 编译命令

```bash
# x86 平台测试
cd pangofly && mkdir -p build && cd build
cmake .. && make -j$(nproc)

# K230 RT-Smart 编译
cd k230_pangofly
make k230_rtos_lckfb_defconfig
make -j$(nproc)
```

---

## 四、使用示例

### 4.1 定义消息结构

```cpp
#include "pangofly/pangofly.h"
#include "idl/container/vector.h"
#include "idl/container/pf_string.h"

struct Point3D {
    float x;
    float y;
    float z;
};

struct FaceDetectionResult {
    int32_t id;
    float score;
    Point3D bbox_min;
    Point3D bbox_max;
    pangofly::String name;
    pangofly::Vector<int32_t> landmarks;
};

struct DetectionResults {
    int64_t timestamp;
    pangofly::Vector<FaceDetectionResult> faces;
};
```

### 4.2 Writer 端代码

```cpp
void WriterProcess() {
    pangofly::Init(nullptr);
    
    auto node = pangofly::CreateNode("detection_node");
    auto writer = node->CreateWriter<DetectionResults>("face_channel");
    
    DetectionResults results;
    results.timestamp = 1234567890;
    
    FaceDetectionResult face;
    face.id = 1;
    face.score = 0.95f;
    face.name = "Alice";
    face.landmarks.push_back(10);
    face.landmarks.push_back(20);
    
    results.faces.push_back(face);
    writer->Write(results);
}
```

### 4.3 Reader 端代码

```cpp
void ReaderProcess() {
    pangofly::Init(nullptr);
    
    auto node = pangofly::CreateNode("detection_node");
    auto reader = node->CreateReader<DetectionResults>("face_channel");
    
    DetectionResults results;
    if (reader->Read(results)) {
        std::cout << "Timestamp: " << results.timestamp << std::endl;
        for (const auto& face : results.faces) {
            std::cout << "Face: " << face.name.c_str() 
                    << ", Score: " << face.score << std::endl;
        }
    }
}
```

---

## 五、性能测试

### 5.1 测试环境

| 项目 | 配置 |
|------|------|
| 平台 | K230 (RISC-V 64) |
| 内存 | 1GB DDR3 |
| 消息大小 | 1KB ~ 64KB |

### 5.2 测试结果

| 消息大小 | 吞吐量 (MB/s) | 延迟 (us) |
|----------|---------------|-----------|
| 1KB | 850 | 1.2 |
| 4KB | 1200 | 3.3 |
| 16KB | 1500 | 10.7 |
| 64KB | 1800 | 35.6 |

### 5.3 对比分析

与传统消息队列对比：

| 指标 | Pangofly | 传统队列 |
|------|----------|----------|
| 吞吐量 | 1.8 GB/s | 150 MB/s |
| 延迟 | 1-36 us | 50-200 us |
| 内存拷贝 | 零拷贝 | 2-3 次拷贝 |

---

## 六、关键代码解析

### 6.1 块分配器核心逻辑

```cpp
void* BlockAllocator::allocate(size_t size) {
    BlockHeader* block = acquire_block();
    if (!block) return nullptr;
    
    size_t offset = sizeof(BlockHeader);
    void* ptr = reinterpret_cast<char*>(block) + offset;
    block->used_size = static_cast<uint32_t>(size);
    return ptr;
}

void BlockAllocator::deallocate_block(void* ptr) {
    BlockHeader* block = reinterpret_cast<BlockHeader*>(
        reinterpret_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    release_block(block);  // 直接归还，不调用析构
}
```

### 6.2 预留区域配置

在 `lwp_arch.h` 中定义：

```cpp
#define PANGOFLY_RESERVE_ADDR  0x120000000ULL
#define PANGOFLY_RESERVE_SIZE  0x80000000ULL  // 256MB
#define PANGOFLY_RESERVE_END   (PANGOFLY_RESERVE_ADDR + PANGOFLY_RESERVE_SIZE)
```

内存分配时自动跳过预留区域：

```cpp
bool lwp_is_in_pangofly_reserve(uint64_t addr) {
    return (addr >= PANGOFLY_RESERVE_ADDR) && (addr < PANGOFLY_RESERVE_END);
}
```

---

## 七、总结与展望

### 7.1 已实现功能

- ✅ 共享内存基础通信
- ✅ 固定地址映射
- ✅ Non-POD 类型支持（Vector、String）
- ✅ 智能指针自动回收
- ✅ RT-Smart 内核预留区域
- ✅ x86 跨平台测试

### 7.2 未来计划

- [ ] IDL 编译器自动生成代码
- [ ] 多通道并发支持
- [ ] 消息压缩传输
- [ ] 分布式通信扩展
- [ ] ROS 兼容接口

### 7.3 项目地址

- **主仓库**: [k230_pangofly](https://github.com/sgf201/k230_pangofly)
- **Pangofly 子模块**: [pangofly](https://github.com/sgf201/pangofly)

---

## 八、附录

### 8.1 目录结构

```
k230_pangofly/
├── pangofly/              # 共享内存通信库
│   ├── idl/               # IDL 定义和容器类
│   │   ├── allocator/     # 块分配器
│   │   └── container/     # Vector、String
│   └── pangofly/          # 核心通信实现
├── src/                   # RT-Smart 内核
└── README.md              # 使用文档
```

### 8.2 编译产物

```
build/
├── libpangofly.so         # 共享库
├── pangofly_test          # POD 测试
└── pangofly_nonpod_test   # Non-POD 测试
```

---

*欢迎各位开发者试用并提出宝贵意见！如有问题或建议，欢迎在 GitHub 上提交 Issue。*
