# Pangofly Non-POD 数据类型支持设计

## 概述

本文档描述 Pangofly 框架对 Non-POD（Plain Old Data）类型数据的支持方案。通过 IDL（接口定义语言）定义消息结构，使用 IDC 编译器生成 C++ 代码，并在共享内存中实现自定义内存分配器，使用户能够像使用 std::vector 一样操作消息中的元素。

## 目标

1. **类型安全**：通过 IDL 定义编译时类型检查
2. **跨进程**：支持跨进程传递复杂的 C++ 对象
3. **高性能**：零拷贝或最小拷贝
4. **易用性**：提供类似 std::vector 的接口
5. **灵活性**：支持动态大小的集合类型

## 架构设计

### 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                        用户代码                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  pangofly::Vector<T>  (STL 兼容接口)                 │   │
│  │  ┌─────────────────────────────────────────────────┐ │   │
│  │  │  自定义分配器 (SharedMemoryAllocator<T>)        │ │   │
│  │  │  ┌─────────────────────────────────────────┐   │ │   │
│  │  │  │  共享内存块 (PangoflyChunk)             │   │ │   │
│  │  │  │  ┌─────────────────────────────────┐    │   │ │   │
│  │  │  │  │  内存分配器 (SlabAllocator)     │    │   │ │   │
│  │  │  │  │  ┌─────────────────────────┐   │    │   │ │   │
│  │  │  │  │  │  共享内存 (ShmSegment)   │   │    │   │ │   │
│  │  │  │  │  └─────────────────────────┘   │    │   │ │   │
│  │  │  │  └─────────────────────────────────┘   │    │ │   │
│  │  │  └─────────────────────────────────────────┘   │   │
│  │  └─────────────────────────────────────────────────┘   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### IDL 定义示例

```idl
// example.idl
namespace pangofly.examples {

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
    string name;  // 可变长字符串
};

struct DetectionResults {
    vector<FaceDetectionResult> faces;  // 动态数组
    int64_t timestamp;
};

}
```

### 生成的 C++ 代码

```cpp
// example.h.auto (IDC 编译器生成)
#pragma once
#include "pangofly/idl/pangofly_idl.h"

namespace pangofly::examples {

struct Point3D {
    float x;
    float y;
    float z;
};

class FaceDetectionResult {
public:
    int32_t& id();
    float& score();
    Point3D& bbox_min();
    Point3D& bbox_max();
    pangofly::String& name();
};

class DetectionResults {
public:
    pangofly::Vector<FaceDetectionResult>& faces();
    int64_t& timestamp();
};

} // namespace pangofly::examples
```

## IDL 语法设计

### 支持的类型

#### 基本类型
| IDL 类型 | C++ 类型 | 描述 |
|----------|----------|------|
| bool | bool | 布尔值 |
| int8 | int8_t | 8位有符号整数 |
| uint8 | uint8_t | 8位无符号整数 |
| int16 | int16_t | 16位有符号整数 |
| uint16 | uint16_t | 16位无符号整数 |
| int32 | int32_t | 32位有符号整数 |
| uint32 | uint32_t | 32位无符号整数 |
| int64 | int64_t | 64位有符号整数 |
| uint64 | uint64_t | 64位无符号整数 |
| float32 | float | 32位浮点数 |
| float64 | double | 64位浮点数 |

#### 复合类型
| IDL 类型 | C++ 类型 | 描述 |
|----------|----------|------|
| struct | class | 结构体（用户定义类型） |
| vector<T> | pangofly::Vector<T> | 动态数组 |
| string | pangofly::String | 可变长字符串 |
| fixed_array<T, N> | std::array<T, N> | 固定大小数组 |

### 语法规则

```bnf
<idl_file>       ::= { <namespace_decl> | <struct_def> }
<namespace_decl>  ::= 'namespace' <ident> '{' { <struct_def> } '}'
<struct_def>     ::= 'struct' <ident> '{' { <field_def> } '}'
<field_def>      ::= <type> <ident> ';'
<type>           ::= <prim_type> | <user_type> | <vector_type> | <string_type>
<prim_type>      ::= 'bool' | 'int8' | 'uint8' | 'int16' | 'uint16' | 
                      'int32' | 'uint32' | 'int64' | 'uint64' |
                      'float32' | 'float64'
<user_type>      ::= <ident>
<vector_type>    ::= 'vector' '<' <type> '>'
<string_type>    ::= 'string'
```

## 代码生成器设计

### IDC 编译器架构

```
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│  IDL 文件   │ -> │  词法分析器  │ -> │  语法树     │
│  (*.idl)    │    │  (Lexer)    │    │  (AST)      │
└─────────────┘    └──────────────┘    └──────────────┘
                                            │
                                            v
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│  C++ 头文件 │ <- │  代码生成器   │ <- │  类型系统   │
│  (*.h.auto) │    │  (Codegen)  │    │  (Symbols)  │
└─────────────┘    └──────────────┘    └─────────────┘
```

### 代码生成模板

#### 类定义生成

```cpp
// Vector 类模板
template<typename T>
class Vector {
public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;
    
    Vector(SharedMemoryAllocator<T>& alloc);
    
    void push_back(const T& value);
    void push_back(T&& value);
    template<typename... Args>
    T& emplace_back(Args&&... args);
    
    T& operator[](size_t idx);
    const T& operator[](size_t idx) const;
    
    size_t size() const;
    bool empty() const;
    void clear();
    
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    
private:
    SharedMemoryAllocator<T>* allocator_;
    T* data_;
    size_t size_;
    size_t capacity_;
};
```

#### 自定义分配器

```cpp
template<typename T>
class SharedMemoryAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    
    template<typename U>
    struct rebind {
        using other = SharedMemoryAllocator<U>;
    };
    
    SharedMemoryAllocator(void* shm_base, size_t shm_size);
    
    T* allocate(size_type n);
    void deallocate(T* p, size_type n);
    
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args);
    
    template<typename U>
    void destroy(U* p);
    
    template<typename U>
    bool operator==(const SharedMemoryAllocator<U>& other) const;
    
    template<typename U>
    bool operator!=(const SharedMemoryAllocator<U>& other) const;
    
private:
    SlabAllocator* slab_;
    void* shm_base_;
};
```

## 内存分配器设计

### Slab 分配器

采用 Slab 分配器管理共享内存，适合固定大小对象的频繁分配释放：

```
┌────────────────────────────────────────────────────────────┐
│                    共享内存块 (PangoflyChunk)              │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐      │
│  │ Slab 64B │ │Slab 128B │ │Slab 256B │ │Slab 512B │ ... │
│  ├──────────┤ ├──────────┤ ├──────────┤ ├──────────┤      │
│  │ ┌─┬─┬─┐ │ │ ┌──┬──┐  │ │ ┌───┬───┐│ │┌────┬────┐│      │
│  │ │█│█│█│ │ │ │██│██│  │ │ │███│███││ ││████│████││      │
│  │ │█│ │█│ │ │ │██│  │  │ │ │███│  ││ ││████│    ││      │
│  │ │█│█│█│ │ │ │  │██│  │ │ │   │███││ ││    │████││      │
│  │ └─┴─┴─┘ │ │ └──┴──┘  │ │ └───┴───┘│ │└────┴────┘│      │
│  │ (空闲)  │ │ (部分使用) │ │ (部分使用)│ │ (部分使用)│      │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘      │
└────────────────────────────────────────────────────────────┘
```

### 内存布局

```cpp
struct PangoflyChunk {
    uint64_t magic;              // 魔数: 0x50414E474F464C59 ("PANGOFLEY")
    uint32_t version;            // 版本号
    uint32_t total_size;        // 块总大小
    uint32_t used_size;         // 已使用大小
    uint32_t num_vectors;       // Vector 数量
    uint32_t num_strings;       // String 数量
    uint32_t flags;             // 标志位
    
    // 元数据区 (固定大小)
    SlabMetadata slabs[NUM_SLAB_SIZES];
    
    // 数据区 (可变大小)
    // ┌─────────────────┬─────────────────┐
    // │   Slab 64B      │   Slab 128B    │ ...
    // │  ┌──┬──┬──┐     │  ┌───┬───┐    │
    // │  │  │  │  │     │  │   │   │    │
    // │  └──┴──┴──┘     │  └───┴───┘    │
    // └─────────────────┴─────────────────┘
};
```

## STL 兼容接口设计

### Vector 接口

```cpp
namespace pangofly {

template<typename T>
class Vector {
public:
    // 类型别名
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    // 构造函数
    Vector();
    Vector(size_type count, const T& value);
    Vector(size_type count);
    Vector(const Vector& other);
    Vector(Vector&& other) noexcept;
    
    // 赋值
    Vector& operator=(const Vector& other);
    Vector& operator=(Vector&& other) noexcept;
    
    // 元素访问
    reference at(size_type pos);
    const_reference at(size_type pos) const;
    reference operator[](size_type pos);
    const_reference operator[](size_type pos) const;
    reference front();
    const_reference front() const;
    reference back();
    const_reference back() const;
    pointer data() noexcept;
    const_pointer data() const noexcept;
    
    // 迭代器
    iterator begin() noexcept;
    const_iterator begin() const noexcept;
    iterator end() noexcept;
    const_iterator end() const noexcept;
    reverse_iterator rbegin() noexcept;
    const_reverse_iterator rbegin() const noexcept;
    reverse_iterator rend() noexcept;
    const_reverse_iterator rend() const noexcept;
    const_iterator cbegin() const noexcept;
    const_iterator cend() const noexcept;
    const_reverse_iterator crbegin() const noexcept;
    const_reverse_iterator crend() const noexcept;
    
    // 容量
    [[nodiscard]] bool empty() const noexcept;
    size_type size() const noexcept;
    size_type capacity() const noexcept;
    size_type max_size() const noexcept;
    void reserve(size_type new_cap);
    void shrink_to_fit();
    
    // 修改器
    void clear() noexcept;
    iterator insert(iterator pos, const T& value);
    iterator insert(iterator pos, T&& value);
    iterator insert(iterator pos, size_type count, const T& value);
    template<typename InputIt>
    iterator insert(iterator pos, InputIt first, InputIt last);
    iterator insert(iterator pos, std::initializer_list<T> ilist);
    template<typename... Args>
    iterator emplace(iterator pos, Args&&... args);
    iterator erase(iterator pos);
    iterator erase(iterator first, iterator last);
    void push_back(const T& value);
    void push_back(T&& value);
    template<typename... Args>
    reference emplace_back(Args&&... args);
    void pop_back();
    void resize(size_type count);
    void resize(size_type count, const value_type& value);
    void swap(Vector& other) noexcept;
    
private:
    T* data_;
    size_type size_;
    size_type capacity_;
    SharedMemoryAllocator<T>* allocator_;
};

} // namespace pangofly
```

### String 接口

```cpp
namespace pangofly {

class String {
public:
    using value_type = char;
    using pointer = char*;
    using const_pointer = const char*;
    using reference = char&;
    using const_reference = const char&;
    using iterator = char*;
    using const_iterator = const char*;
    using size_type = size_t;

    // 构造函数
    String();
    String(const char* s);
    String(const char* s, size_type count);
    String(size_type count, char c);
    String(const String& other);
    String(String&& other) noexcept;
    
    // 赋值
    String& operator=(const char* s);
    String& operator=(const String& other);
    String& operator=(String&& other) noexcept;
    
    // 元素访问
    char& operator[](size_type pos);
    const char& operator[](size_type pos) const;
    char& at(size_type pos);
    const char& at(size_type pos) const;
    char& front();
    const char& front() const;
    char& back();
    const char& back() const;
    const char* c_str() const;
    const char* data() const;
    
    // 迭代器
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    
    // 容量
    bool empty() const;
    size_type size() const;
    size_type length() const;
    size_type capacity() const;
    void reserve(size_type new_cap);
    void shrink_to_fit();
    
    // 修改器
    void clear();
    void push_back(char c);
    void pop_back();
    String& append(const char* s);
    String& append(const String& str);
    String& operator+=(const String& str);
    String& operator+=(const char* s);
    void resize(size_type count);
    void resize(size_type count, char c);
    void swap(String& other);
    
    // 比较
    bool operator==(const String& other) const;
    bool operator!=(const String& other) const;
    
private:
    char* data_;
    size_type size_;
    size_type capacity_;
    SharedMemoryAllocator<char>* allocator_;
};

} // namespace pangofly
```

## 使用示例

### Writer 端

```cpp
#include "example.h.auto"
#include "pangofly/pangofly.h"

int main() {
    auto node = pangofly::CreateNode("writer_node");
    auto channel = node->CreateWriter<DetectionResults>("detection_results");
    
    DetectionResults results;
    results.timestamp(1234567890);
    
    // 添加人脸检测结果
    auto& face1 = results.faces().emplace_back();
    face1.id(1);
    face1.score(0.95f);
    face1.bbox_min().x(100.0f);
    face1.bbox_min().y(100.0f);
    face1.bbox_min().z(0.0f);
    face1.bbox_max().x(200.0f);
    face1.bbox_max().y(200.0f);
    face1.bbox_max().z(0.0f);
    face1.name("Alice");
    
    auto& face2 = results.faces().emplace_back();
    face2.id(2);
    face2.score(0.87f);
    face2.name("Bob");
    
    channel->Send(std::move(results));
    
    return 0;
}
```

### Reader 端

```cpp
#include "example.h.auto"
#include "pangofly/pangofly.h"

int main() {
    auto node = pangofly::CreateNode("reader_node");
    auto channel = node->CreateReader<DetectionResults>("detection_results");
    
    while (true) {
        auto results = channel->Receive();
        
        std::cout << "Timestamp: " << results.timestamp() << std::endl;
        std::cout << "Faces: " << results.faces().size() << std::endl;
        
        for (const auto& face : results.faces()) {
            std::cout << "  Face " << face.id() 
                      << ": " << face.name().c_str()
                      << ", score: " << face.score()
                      << std::endl;
        }
    }
    
    return 0;
}
```

## 实现计划

### Phase 1: 基础设施
- [ ] 设计 IDL 词法分析器 (Lexer)
- [ ] 设计 IDL 语法分析器 (Parser)
- [ ] 实现 AST 节点类型
- [ ] 实现符号表管理

### Phase 2: 代码生成
- [ ] 实现 Vector 模板类
- [ ] 实现 String 类
- [ ] 实现 SharedMemoryAllocator
- [ ] 实现 SlabAllocator
- [ ] 实现代码生成器模板

### Phase 3: 集成测试
- [ ] 编写 IDC 编译器
- [ ] 创建示例 IDL 文件
- [ ] 端到端测试
- [ ] 性能测试

## 约束与限制

1. **类型限制**：IDL 不支持指针、动态多态（虚函数）
2. **大小限制**：消息总大小受共享内存块大小限制
3. **跨进程**：Writer 和 Reader 必须使用相同的 IDL 定义
4. **编译时**：IDC 编译需要独立的编译步骤

## 参考资料

- [C++20 std::vector 接口](https://en.cppreference.com/w/cpp/container/vector)
- [C++20 std::string 接口](https://en.cppreference.com/w/cpp/string/basic_string)
- [Slab Allocator](https://en.wikipedia.org/wiki/Slab_allocation)
- [Protocol Buffers](https://developers.google.com/protocol-buffers)
