// pangofly v2 示例消息定义
//
// 手写版等价于 idc 从 IDL 生成的结果：
//   * 唯一的构造函数接收 shm::Arena&
//   * 容器成员从该 arena 分配
//   * 用 PANGOFLY_SHM_MESSAGE 声明类型可用于共享内存

#ifndef PANGOFLY_EXAMPLE_MESSAGES_H_
#define PANGOFLY_EXAMPLE_MESSAGES_H_

#include <cstdint>

#include "core/shm/allocator.h"
#include "core/shm/arena.h"

namespace example {

struct Detection {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  float score;

  // 纯 POD：既能携带 arena 构造，也能被 shm::vector 的 resize() 默认构造
  Detection() : x(0), y(0), w(0), h(0), score(0.0f) {}
  explicit Detection(::pangofly::shm::Arena& arena) : Detection() { (void)arena; }
};

struct ImageFrame {
  int64_t timestamp_ns;
  int32_t width;
  int32_t height;
  int32_t stride;
  ::pangofly::shm::vector<uint8_t> pixels;
  ::pangofly::shm::vector<Detection> detections;
  ::pangofly::shm::string label;

  explicit ImageFrame(::pangofly::shm::Arena& arena)
      : timestamp_ns(0),
        width(0),
        height(0),
        stride(0),
        pixels(arena),
        detections(arena),
        label(arena) {}
};

}  // namespace example

PANGOFLY_SHM_MESSAGE(::example::Detection)
PANGOFLY_SHM_MESSAGE(::example::ImageFrame)

#endif  // PANGOFLY_EXAMPLE_MESSAGES_H_
