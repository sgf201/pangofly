# Pangofly IDL (Interface Definition Language)

## 概述

Pangofly IDL 是一种接口定义语言，用于定义跨节点通信的数据类型。它遵循 **OMG IDL 4.2 / DDS-XTypes** 标准语法，支持从 IDL 文件自动生成 C++ 头文件。

IDL 文件是节点间通信数据结构的唯一可信来源（Single Source of Truth），所有节点间传递的消息类型都必须通过 IDL 定义，由编译器自动生成代码。

---

## 快速开始

### 1. 编写 IDL 文件

```idl
module MyModule {

    struct Point {
        long x;
        long y;
    };

    struct SensorData {
        long long timestamp;
        float temperature;
        sequence<octet> raw_data;
        sequence<Point> points;
    };
};
```

### 2. 生成 C++ 头文件

```bash
# 使用 idc 编译器
idc -o output.h input.idl
```

### 3. 在 CMake 中集成

```cmake
# CMakeLists.txt 中自动生成
execute_process(
    COMMAND ${HOST_CXX_COMPILER} -std=c++17
        -I${PANGOFLY_ROOT} -I${PANGOFLY_ROOT}/idl
        -o ${IDC_EXECUTABLE} ${IDC_SOURCES}
)

add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${IDC_EXECUTABLE} -o ${GENERATED_HEADER} ${IDL_FILE}
    DEPENDS ${IDL_FILE} ${IDC_SOURCES}
)
```

---

## IDL 语法规范

### 模块（Module）

使用 `module` 关键字定义命名空间，对应 C++ 的 `namespace`。

```idl
module FaceDetection {
    // ... 类型定义 ...
};
```

> **注意**：模块结束的右大括号后**不需要**分号。

### 结构体（Struct）

使用 `struct` 关键字定义结构体，每个字段以 `;` 结尾。

```idl
struct FaceBox {
    long x;
    long y;
    long width;
    long height;
    float score;
    long id;
};
```

> **注意**：结构体结束的右大括号后**必须**有分号。

### 基本数据类型

| IDL 类型 | C++ 类型 | 说明 |
|---------|---------|------|
| `boolean` | `bool` | 布尔值 |
| `char` | `char` | 8位字符 |
| `octet` | `uint8_t` | 8位无符号整数（字节） |
| `short` | `int16_t` | 16位有符号整数 |
| `unsigned short` | `uint16_t` | 16位无符号整数 |
| `long` | `int32_t` | 32位有符号整数 |
| `unsigned long` | `uint32_t` | 32位无符号整数 |
| `long long` | `int64_t` | 64位有符号整数 |
| `unsigned long long` | `uint64_t` | 64位无符号整数 |
| `float` | `float` | 32位浮点数 |
| `double` | `double` | 64位浮点数 |
| `string` | `pangofly::String` | 字符串 |

### 序列（Sequence）

动态长度数组，对应 C++ 的 `pangofly::Vector<T>`（兼容 `std::vector` 接口）。

```idl
sequence<octet> data;           // 字节数组
sequence<FaceBox> faces;        // 人脸框数组
sequence<long> indices;         // 索引数组
```

### 定长数组（Fixed Array）

固定长度数组，对应 C++ 的 `std::array<T, N>`。数据内联存储，无堆分配，适合大小已知的场景。

```idl
struct Example {
    long indices[10];         // 10个 int32_t 的定长数组
    octet buffer[256];        // 256字节的定长缓冲区
    float coords[3];          // 3个 float 的坐标数组
};
```

> **注意**：数组大小必须是正整数字面量，不支持变量或表达式。

### 注释

支持单行注释 `//`：

```idl
// 这是一条注释
struct Example {
    long value;  // 字段注释
};
```

> **注意**：暂不支持 `/* */` 多行注释。

---

## 生成的 C++ 代码结构

每个 IDL `struct` 生成对应的 C++ `struct`，包含以下成员：

### 公共字段

结构体的所有字段直接作为公共成员变量，可直接读写。

### GetTypeName()

返回类型的完全限定名，用于类型标识。

```cpp
static const char* GetTypeName();
```

### 构造函数

```cpp
// 默认构造函数（所有基本类型字段初始化为 0）
MyStruct();

// BlockAllocator 构造函数（用于共享内存分配）
explicit MyStruct(pangofly::BlockAllocator* allocator);
```

### CalculateSize()

计算结构体及所有动态字段（sequence）的总字节大小，用于共享内存序列化。

```cpp
size_t CalculateSize() const;
```

### 示例

IDL 定义：
```idl
module Example {
    struct Point {
        long x;
        long y;
    };
};
```

生成的 C++ 代码：
```cpp
namespace Example {
    struct Point {
        static const char* GetTypeName() { return "Example::Point"; }

        int32_t x;
        int32_t y;

        Point() { x = 0; y = 0; }
        explicit Point(pangofly::BlockAllocator* allocator) { x = 0; y = 0; }

        size_t CalculateSize() const {
            size_t total = sizeof(*this);
            return total;
        }
    };
}
```

---

## 编写规范

### 命名约定

| 元素 | 约定 | 示例 |
|------|------|------|
| module 名 | PascalCase（大驼峰） | `FaceDetection`, `SensorData` |
| struct 名 | PascalCase（大驼峰） | `ImageFrame`, `FaceBox` |
| 字段名 | snake_case（小写下划线） | `frame_id`, `processing_time_ms` |
| IDL 文件名 | snake_case.idl | `face_detection.idl`, `sensor_data.idl` |

### 文件组织

- IDL 文件统一放在 `pangofly/examples/` 目录下
- 一个 IDL 文件对应一个功能模块
- 文件名与主 module 名保持一致（小写形式）

### 最佳实践

1. **IDL 是唯一数据源**：绝不手动修改生成的头文件
2. **使用标准类型**：`module`, `sequence`, `octet`, `long` 等 OMG 标准语法
3. **使用有意义的命名**：结构体和字段名应清晰表达含义
4. **添加注释**：为复杂的结构体和字段添加注释说明
5. **版本管理**：IDL 文件纳入版本控制，修改需向后兼容
6. **嵌套结构体**：复杂类型应拆分为多个 struct，通过引用组合

### 不应做的事

- ❌ 不要手动编辑生成的 `.h` 文件
- ❌ 不要在生成的头文件中添加自定义逻辑
- ❌ 不要在 IDL 中定义函数/接口（当前仅支持数据类型）

---

## IDL 编译器 (idc)

### 构建

```bash
g++ -std=c++17 -I. -Iidl -o idc \
    idl/tools/idc.cc \
    idl/lexer/lexer.cc \
    idl/parser/parser.cc \
    idl/codegen/code_generator.cc
```

### 使用

```bash
# 从文件生成
idc -o output.h input.idl

# 从源码字符串生成
idc -I "struct Point { long x; };" -o output.h

# 输出到标准输出
idc input.idl
```

### 源码结构

```
idl/
├── lexer/          # 词法分析器
│   ├── lexer.h
│   └── lexer.cc
├── parser/         # 语法分析器
│   ├── parser.h
│   └── parser.cc
├── ast/            # 抽象语法树
│   └── ast.h
├── codegen/        # 代码生成器
│   ├── code_generator.h
│   └── code_generator.cc
├── container/      # 运行时容器（Vector, String）
│   ├── vector.h
│   └── string.h
├── allocator/      # 内存分配器
│   └── block_allocator.h
├── tools/
│   └── idc.cc      # 编译器入口
└── README.md       # 本文档
```

---

## 与 DDS IDL 的关系

Pangofly IDL 语法子集兼容 OMG IDL 4.2 / DDS-XTypes 标准，主要差异：

| 特性 | DDS IDL | Pangofly IDL |
|------|---------|--------------|
| module | ✅ | ✅ |
| struct | ✅ | ✅ |
| 基本类型 | ✅ | ✅（子集） |
| sequence | ✅ | ✅ |
| array | ✅ | ✅ |
| enum | ✅ | 计划中 |
| union | ✅ | 不支持 |
| typedef | ✅ | 计划中 |
| const | ✅ | 不支持 |
| interface | ✅ | 不支持 |
| 注解 (@key 等) | ✅ | 不支持 |
| 多行注释 `/* */` | ✅ | 计划中 |

> 使用标准 IDL 语法的好处：未来可平滑迁移到完整 DDS 实现，或与其他 DDS 系统互操作。
