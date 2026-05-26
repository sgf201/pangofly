# Pangofly Gesture Application

手势识别应用，使用Pangofly进行进程间通信。

## 组件

- **gesture_publisher_simple**: 发布模拟图像数据（测试用）
- **gesture_subscriber_simple**: 接收并验证图像数据（测试用）
- **gesture_publisher_pod**: 发布简单POD消息（已测试）
- **gesture_subscriber_pod**: 接收简单POD消息（已测试）

## 构建

在开发机上构建K230版本：
```bash
cd gesture
make clean
make
```

## 运行测试

### 在K230上

```bash
# 1. 设置库路径
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH

# 2. 在终端1启动发布者
./gesture_publisher_simple &

# 3. 在终端2启动订阅者
./gesture_subscriber_simple
```

或者使用测试脚本：
```bash
/root/run_test.sh
```

## 架构

应用展示了如何通过Pangofly共享内存传输大型图像数据：

1. **消息结构**：使用POD类型，包含时间戳和图像数据
2. **动态大小**：自动根据消息大小分配共享内存
3. **发布/订阅模式**：解耦图像采集和识别处理

## 关键修改

为了让Pangofly支持大消息传输（如196KB图像），进行了以下修改：

### 1. shm_conf.cc - 支持更大的消息大小
```cpp
uint32_t ShmConf::GetCeilingMessageSize(const uint32_t& real_msg_size) {
  // 支持从64字节到8MB的消息大小
  if (real_msg_size <= 64) return 64;
  if (real_msg_size <= 128) return 128;
  // ... 增加到8MB
  return 8388608;
}
```

### 2. writer_base.h & reader_base.h - 传递实际消息大小
```cpp
// 创建Segment时传递sizeof(MessageT)
segment_(std::make_shared<PANGOFLY_SEGMENT_TYPE>(
    "/shm", std::hash<std::string>()(channel_name), sizeof(MessageT)))
```

## 测试结果

✅ 成功传输196608字节（256x256x3）的图像帧
✅ ceiling size自动调整为262144字节
✅ 数据完整性验证通过

## 下一步

- 集成真实V4L2摄像头采集
- 添加NNCASE手势识别模型
- 优化帧率和缓冲区管理
