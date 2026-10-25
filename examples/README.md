# Examples

## common/shm_example — 基础收发示例（v2）

三个可执行文件演示最小可用的零拷贝通道：

| 程序 | 作用 |
|------|------|
| `shm_example` | 单进程自测：写满所有块 → 逐条读回 → 校验内容 → 再写一轮 |
| `shm_writer` | 写端：直接在共享内存里构造 `ImageFrame`（含 `shm::vector` 像素与检测框） |
| `shm_reader` | 读端：直接读共享内存中的对象，打印消息指针以核对同地址映射 |

构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

运行：

```bash
./build/examples/common/shm_example/shm_example

# 跨进程（先起读端，再起写端）
./build/examples/common/shm_example/shm_reader 10 4000 &
./build/examples/common/shm_example/shm_writer 10 50
```

跨进程运行时，写端与读端打印的消息指针完全相同（例如 `0x6000000009f0`），
这是「同地址映射」生效的直接证据——容器内的指针因此在两端都有效。

消息定义见 [messages.h](common/shm_example/messages.h)，等价于 `idc` 从 IDL 生成的结果：

- 唯一的构造函数接收 `shm::Arena&`
- 容器成员从该 arena 分配
- 用 `PANGOFLY_SHM_MESSAGE(Type)` 声明类型可用于共享内存
