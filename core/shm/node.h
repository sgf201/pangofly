#ifndef PANGOFLY_V2_NODE_H_
#define PANGOFLY_V2_NODE_H_

// ============================================================================
// Writer<T> / Reader<T> — v2 消息收发接口
//
// 写端：
//   Writer<Msg> w(seg);
//   auto s = w.Loan();          // 从共享内存借一块，就地构造 Msg
//   s->points.push_back(...);   // 直接写共享内存，无拷贝
//   w.Publish(std::move(s));    // 发布
//
// 读端：
//   Reader<Msg> r(seg);
//   auto v = r.Acquire();       // 取一条，不拷贝
//   use(v->points[i]);          // v 是 RAII 视图，出作用域自动归还
//
// 约束：Msg 必须提供 explicit Msg(shm::Arena&)，且平凡析构。
// ============================================================================

#include <time.h>

#include <cstdint>
#include <memory>
#include <new>
#include <utility>

#include "core/shm/allocator.h"
#include "core/shm/arena.h"
#include "core/shm/message.h"
#include "core/shm/segment.h"

namespace pangofly {
namespace shm {

namespace detail {
inline uint64_t NowNs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}
}  // namespace detail

template <typename T>
class Writer {
  static_assert(IsShmCompatibleV<T>,
                "Writer<T>: T must be declared shm-compatible; put "
                "PANGOFLY_SHM_MESSAGE(T) after its definition");

 public:
  explicit Writer(std::shared_ptr<Segment> seg) : seg_(std::move(seg)) {}

  // 借一块并就地构造 T。无可用块（背压）时返回无效 Sample。
  Sample<T> Loan() {
    if (!seg_ || !seg_->valid()) return {};

    BlockHeader* b = seg_->TryAcquireWritable();
    if (b == nullptr) return {};

    Arena* arena = seg_->BlockArena(b);
    if (arena == nullptr) {
      seg_->AbortWrite(b);
      return {};
    }

    // 消息对象本身也由 arena 分配：这样一块内存的回收只需 arena->Reset()，
    // 块头与消息体互不覆盖，且 T 与它内部的容器共享同一个 arena。
    void* slot = arena->Allocate(sizeof(T), alignof(T));
    if (slot == nullptr) {
      seg_->AbortWrite(b);
      return {};
    }

    T* msg = new (slot) T(*arena);

    auto* payload = static_cast<uint8_t*>(seg_->BlockPayload(b));
    const uint32_t msg_offset =
        static_cast<uint32_t>(static_cast<uint8_t*>(slot) - payload);

    return Sample<T>(seg_, b, msg, arena, msg_offset);
  }

  // 发布。消息体不析构：块回收时直接 arena->Reset()。
  bool Publish(Sample<T>&& s) {
    if (!s.valid()) return false;

    BlockHeader* b = s.block();
    const uint32_t msg_offset = s.msg_offset();
    seg_->Publish(b, msg_offset, detail::NowNs());
    s.MarkPublished();
    return true;
  }

  void Abort(Sample<T>&& s) {
    if (s.valid()) {
      seg_->AbortWrite(s.block());
      s.MarkPublished();
    }
  }

  // 就地填充：fn(T&) 在共享内存中的对象上执行，无拷贝
  template <typename Fn>
  bool Write(Fn&& fn) {
    Sample<T> s = Loan();
    if (!s.valid()) return false;
    fn(*s);
    return Publish(std::move(s));
  }

  uint32_t Capacity() const { return seg_ ? seg_->BlockCapacity() : 0; }
  uint32_t BlockNum() const { return seg_ ? seg_->BlockNum() : 0; }
  // keep_last 生效值：收端最多保留的未消费消息条数（0 表示不限）
  uint32_t History() const { return seg_ ? seg_->History() : 0; }

 private:
  std::shared_ptr<Segment> seg_;
};

template <typename T>
class Reader {
  static_assert(IsShmCompatibleV<T>,
                "Reader<T>: T must be declared shm-compatible; put "
                "PANGOFLY_SHM_MESSAGE(T) after its definition");

 public:
  explicit Reader(std::shared_ptr<Segment> seg) : seg_(std::move(seg)) {}

  // 取一条 seq 大于上次的已发布消息（FIFO，不丢消息）
  View<T> Acquire() {
    if (!seg_ || !seg_->valid()) return {};
    BlockHeader* b = seg_->AcquireRead(&since_seq_);
    if (b == nullptr) return {};
    const T* msg = seg_->MessageOf<T>(b);
    if (msg == nullptr) {
      seg_->ReleaseRead(b);
      return {};
    }
    return View<T>(seg_, b, msg);
  }

  // 阻塞等待，超时返回无效 View
  View<T> Wait(uint64_t timeout_ns) {
    if (!seg_ || !seg_->valid()) return {};
    BlockHeader* b = seg_->WaitRead(&since_seq_, timeout_ns);
    if (b == nullptr) return {};
    const T* msg = seg_->MessageOf<T>(b);
    if (msg == nullptr) {
      seg_->ReleaseRead(b);
      return {};
    }
    return View<T>(seg_, b, msg);
  }

  // 回调式：View 在回调期间有效
  template <typename Fn>
  bool Observe(Fn&& fn) {
    View<T> v = Acquire();
    if (!v.valid()) return false;
    fn(*v);
    return true;
  }

  uint64_t since_seq() const { return since_seq_; }
  uint32_t BlockNum() const { return seg_ ? seg_->BlockNum() : 0; }
  // keep_last 生效值：本 reader 最多能看到的未消费消息条数（0 表示不限）
  uint32_t History() const { return seg_ ? seg_->History() : 0; }

 private:
  std::shared_ptr<Segment> seg_;
  uint64_t since_seq_ = 0;
};

}  // namespace shm
}  // namespace pangofly

#endif  // PANGOFLY_V2_NODE_H_
