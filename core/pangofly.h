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

  template <typename MessageT>
  std::shared_ptr<shm::Reader<MessageT>> CreateReader(
      const std::string& channel, const shm::SegmentConfig& cfg = {}) {
    auto seg = GetSegment(channel, cfg, detail::MessageTypeName<MessageT>());
    if (!seg) return nullptr;
    return std::make_shared<shm::Reader<MessageT>>(seg);
  }

 private:
  explicit Node(std::string name) : name_(std::move(name)) {}

  friend std::unique_ptr<Node> CreateNode(const std::string&);

  std::string name_;
  std::mutex mu_;
  std::map<std::string, std::shared_ptr<shm::Segment>> segments_;
};

std::unique_ptr<Node> CreateNode(const std::string& node_name);

}  // namespace pangofly

#endif  // PANGOFLY_PANGOFLY_H_
