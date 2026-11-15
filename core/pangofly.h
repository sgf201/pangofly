#ifndef PANGOFLY_PANGOFLY_H_
#define PANGOFLY_PANGOFLY_H_

// ============================================================================
// pangofly 门面：进程初始化 + 按 channel 复用 Segment 的 Node
//
// 用法：
//   pangofly::Init();                       // 只做一次地址空间注册
//   auto node = pangofly::CreateNode("app");
//   auto w = node->CreateWriter<Msg>("ch");
//   auto r = node->CreateReader<Msg>("ch");
//
// Writer/Reader 是轻量句柄（只持有 shared_ptr<Segment>），可随时创建多个。
// Segment 则按 channel 在进程内唯一，保证读写端映射到同一地址。
// ============================================================================

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>

#include "core/common/log.h"
#include "core/shm/node.h"
#include "core/shm/region.h"
#include "core/shm/segment.h"

namespace pangofly {
namespace detail {

// 消息类型名：IDL 生成的类型都带 GetTypeName()；手写类型没有时退化为编译期
// 函数签名。两者都只是「两端声明是否一致」的判据，不依赖 RTTI。
template <typename T, typename = void>
struct HasGetTypeName : std::false_type {};
template <typename T>
struct HasGetTypeName<T, decltype((void)T::GetTypeName(), void())> : std::true_type {};

template <typename T>
const char* PrettyTypeName() {
  static const std::string name = __PRETTY_FUNCTION__;
  return name.c_str();
}

template <typename T>
const char* MessageTypeName() {
  if constexpr (HasGetTypeName<T>::value) {
    return T::GetTypeName();
  } else {
    return PrettyTypeName<T>();
  }
}

}  // namespace detail

// 注册共享内存虚拟地址窗口。默认 RegionConfig 即可；重复调用只生效第一次。
bool Init(const char* argv0 = nullptr);

// 只释放进程内资源，不 shm_unlink：读端可能晚于写端启动。
void Shutdown();

class Node {
 public:
  ~Node() = default;

  const std::string& Name() const { return name_; }

  // type_name 用于类型指纹校验：同一 channel 的写端与读端必须传入同一个
  // 消息类型名，否则 attach 失败。CreateWriter / CreateReader 会自动从
  // MessageT::GetTypeName() 取，通常不需要直接调用本函数。
  std::shared_ptr<shm::Segment> GetSegment(const std::string& channel,
                                           const shm::SegmentConfig& cfg,
                                           const char* type_name);

  template <typename MessageT>
  std::shared_ptr<shm::Writer<MessageT>> CreateWriter(
      const std::string& channel, const shm::SegmentConfig& cfg = {}) {
    auto seg = GetSegment(channel, cfg, detail::MessageTypeName<MessageT>());
    if (!seg) return nullptr;
    return std::make_shared<shm::Writer<MessageT>>(seg);
  }

  // 同一 Node 内同一个 channel 只允许一个 Reader。
  //
  // 原因：Reader 持有自己的 since_seq_（已消费到的 seq）。若同一 channel 上
  // 同时存在两个 Reader，它们各自推进进度，写端回收块时无法同时满足两个
  // 读者的进度，容易造成其中一方持续读不到数据（表现为「随机丢帧」）。
  // 需要多处消费同一 channel 时，正确做法是让它们共享同一个 Reader，
  // 或分别放在不同的 Node 里（此时是两个独立的消费端，语义明确）。
  //
  // 返回 nullptr 表示该 channel 上已有存活 Reader。
  template <typename MessageT>
  std::shared_ptr<shm::Reader<MessageT>> CreateReader(
      const std::string& channel, const shm::SegmentConfig& cfg = {}) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = readers_.find(channel);
      if (it != readers_.end()) {
        // weak_ptr 仍可 lock 说明旧 Reader 还活着，拒绝重复创建；
        // 已销毁则允许重建（常见于「先销毁再重建」的用法）。
        std::shared_ptr<void> existing = it->second.lock();
        if (existing) {
          PANGOFLY_ERROR << "[node] reader for channel '" << channel
                         << "' already exists in node '" << name_
                         << "'; reuse it or create another Node" << std::endl;
          return nullptr;
        }
        readers_.erase(it);
      }
    }

    auto seg = GetSegment(channel, cfg, detail::MessageTypeName<MessageT>());
    if (!seg) return nullptr;
    auto reader = std::make_shared<shm::Reader<MessageT>>(seg);

    std::lock_guard<std::mutex> lk(mu_);
    readers_[channel] = reader;
    return reader;
  }

 private:
  explicit Node(std::string name) : name_(std::move(name)) {}

  friend std::unique_ptr<Node> CreateNode(const std::string&);

  std::string name_;
  std::mutex mu_;
  std::map<std::string, std::shared_ptr<shm::Segment>> segments_;
  // 已创建的 Reader，key 为 channel 名。存 weak_ptr 而不是 shared_ptr：
  // Node 不延长 Reader 的生命周期，Reader 由调用方持有；这里只用来查重。
  std::map<std::string, std::weak_ptr<void>> readers_;
};

std::unique_ptr<Node> CreateNode(const std::string& node_name);

}  // namespace pangofly

#endif  // PANGOFLY_PANGOFLY_H_
