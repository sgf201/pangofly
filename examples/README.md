# Examples

## common/shm_example — 基础收发示例（v2）

三个可执行文件演示最小可用的零拷贝通道：

| 程序 | 作用 |
|------|------|
| `shm_example` | 单进程自测：写满所有块 → 逐条读回 → 校验内容 → 再写一轮 |
| `shm_writer` | 写端：直接在共享内存里构造 `ImageFrame`（含 `shm::vector` 像素与检测框） |
| `shm_reader` | 读端：直接读共享内存中的对象，打印消息指针以核对同地址映射 |
| `shm_thread_pipeline` | 跨线程：线程 A 只 `Loan`，线程 B 填充并 `Publish` |

构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

运行：

```bash
./build/examples/common/shm_example/shm_example

# 跨线程：Loan 与填充分属两个线程
./build/examples/common/shm_example/shm_thread_pipeline 50

# 跨进程（先起读端，再起写端）
./build/examples/common/shm_example/shm_reader 10 4000 &
./build/examples/common/shm_example/shm_writer 10 50
```

跨进程运行时，写端与读端打印的消息指针完全相同（例如 `0x6000000009f0`），
这是「同地址映射」生效的直接证据——容器内的指针因此在两端都有效。

### 跨线程：Loan 与填充分离

`shm_thread_pipeline.cc` 演示 `Sample<T>` 的所有权可以跨线程转移：

```cpp
// 线程 A：只借块，不填数据
auto s = writer->Loan();
queue.Push(std::move(s));          // move 进队列，只转移指针

// 线程 B：取出后原地填充并发布
auto s = queue.Pop();
s->pixels.resize(bytes);           // 直接写共享内存
writer->Publish(std::move(s));     // 只翻状态位
```

关键点：

- `Sample<T>` 内部持有 `shared_ptr<Segment>`，所以它可以在 `Writer` 句柄销毁后
  继续存活，不必担心 Segment 被提前释放
- `Sample` 是 move-only，跨线程传递的成本是一个指针，不复制任何消息数据
- 消息对象始终留在共享内存中，两个线程看到的是同一块地址
- `Loan` 与 `Publish` 分离意味着块在 `WRITING` 状态停留更久。若生产者比消费者快，
  块会耗尽并让 `Loan` 返回无效 Sample（背压），示例里用有界队列 + 重试处理
- 单线程场景优先用 `writer->Write(fn)`，它把借块、填充、发布合成一个调用，
  不会出现块被长期占住的问题

消息定义见 [messages.h](common/shm_example/messages.h)，等价于 `idc` 从 IDL 生成的结果：

- 唯一的构造函数接收 `shm::Arena&`
- 容器成员从该 arena 分配
- 用 `PANGOFLY_SHM_MESSAGE(Type)` 声明类型可用于共享内存
