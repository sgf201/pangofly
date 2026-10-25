#ifndef PANGOFLY_V2_MESSAGE_H_
#define PANGOFLY_V2_MESSAGE_H_

// ============================================================================
// Sample<T> / View<T> — 共享内存消息的写端借据与读端视图
//
// Sample<T>：写端从 Segment 借出的块句柄。消息对象 T 由块内 arena 分配，
//            就地构造，因此 s->field 写的就是共享内存本身（零拷贝）。
//            析构时若未 Publish，自动 Abort（归还块）。
// View<T>：  读端视图。构造时 readers++，析构时 readers--。
//            通过 operator-> 直接访问共享内存中的对象，不做任何拷贝。
//
// 生命周期契约
//   * T 的析构函数不会被调用。释放一块内存 = arena->Reset()，O(1)
//   * 因此 T 内只允许使用 shm::vector / shm::string 这类由 arena 管理的容器，
//     不允许 std::vector / std::string / 裸指针等持有堆内存或外部句柄的成员
// ============================================================================

#include <cstdint>
#include <utility>

#include "core/shm/arena.h"
#include "core/shm/block.h"
#include "core/shm/segment.h"

namespace pangofly {
namespace shm {

// ---------------------------------------------------------------------------
// Sample：写端借据
// ---------------------------------------------------------------------------
template <typename T>
class Sample {
 public:
  Sample() = default;

  Sample(Segment* seg, BlockHeader* block, T* msg, Arena* arena, uint32_t msg_offset)
      : seg_(seg), block_(block), msg_(msg), arena_(arena), msg_offset_(msg_offset) {}

  Sample(Sample&& other) noexcept { MoveFrom(other); }

  Sample& operator=(Sample&& other) noexcept {
    if (this != &other) {
      Cleanup();
      MoveFrom(other);
    }
    return *this;
  }

  Sample(const Sample&) = delete;
  Sample& operator=(const Sample&) = delete;

  ~Sample() { Cleanup(); }

  T* operator->() noexcept { return msg_; }
  const T* operator->() const noexcept { return msg_; }
  T& operator*() noexcept { return *msg_; }
  const T& operator*() const noexcept { return *msg_; }

  T* get() noexcept { return msg_; }
  const T* get() const noexcept { return msg_; }

  Arena& arena() noexcept { return *arena_; }
  bool valid() const noexcept { return msg_ != nullptr && block_ != nullptr; }

  BlockHeader* block() const noexcept { return block_; }
  Segment* segment() const noexcept { return seg_; }
  uint32_t msg_offset() const noexcept { return msg_offset_; }

  // 由 Writer::Publish 调用：标记已完成，析构时不再 Abort
  void MarkPublished() noexcept { Reset(); }

 private:
  void MoveFrom(Sample& o) noexcept {
    seg_ = o.seg_;
    block_ = o.block_;
    msg_ = o.msg_;
    arena_ = o.arena_;
    msg_offset_ = o.msg_offset_;
    published_ = o.published_;
    o.Reset();
    o.published_ = true;   // 阻止源对象析构时 Abort
  }

  void Reset() noexcept {
    seg_ = nullptr;
    block_ = nullptr;
    msg_ = nullptr;
    arena_ = nullptr;
    msg_offset_ = 0;
  }

  void Cleanup() noexcept {
    if (!published_ && seg_ != nullptr && block_ != nullptr) {
      seg_->AbortWrite(block_);
    }
    Reset();
  }

  Segment* seg_ = nullptr;
  BlockHeader* block_ = nullptr;
  T* msg_ = nullptr;
  Arena* arena_ = nullptr;
  uint32_t msg_offset_ = 0;
  bool published_ = false;
};

// ---------------------------------------------------------------------------
// View：读端视图
// ---------------------------------------------------------------------------
template <typename T>
class View {
 public:
  View() = default;

  View(Segment* seg, BlockHeader* block, const T* msg)
      : seg_(seg), block_(block), msg_(msg) {}

  View(View&& other) noexcept { MoveFrom(other); }

  View& operator=(View&& other) noexcept {
    if (this != &other) {
      Cleanup();
      MoveFrom(other);
    }
    return *this;
  }

  View(const View&) = delete;
  View& operator=(const View&) = delete;

  ~View() { Cleanup(); }

  const T* operator->() const noexcept { return msg_; }
  const T& operator*() const noexcept { return *msg_; }
  const T* get() const noexcept { return msg_; }

  bool valid() const noexcept { return msg_ != nullptr; }
  uint64_t seq() const noexcept { return block_ ? block_->seq : 0; }
  uint64_t timestamp_ns() const noexcept { return block_ ? block_->timestamp_ns : 0; }

 private:
  void MoveFrom(View& o) noexcept {
    seg_ = o.seg_;
    block_ = o.block_;
    msg_ = o.msg_;
    o.seg_ = nullptr;
    o.block_ = nullptr;
    o.msg_ = nullptr;
  }

  void Cleanup() noexcept {
    if (seg_ != nullptr && block_ != nullptr) {
      seg_->ReleaseRead(block_);
    }
    seg_ = nullptr;
    block_ = nullptr;
    msg_ = nullptr;
  }

  Segment* seg_ = nullptr;
  BlockHeader* block_ = nullptr;
  const T* msg_ = nullptr;
};

}  // namespace shm
}  // namespace pangofly

#endif  // PANGOFLY_V2_MESSAGE_H_
