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

#include "core/shm/node.h"
#include "core/shm/region.h"
#include "core/shm/segment.h"

namespace pangofly {

// 注册共享内存虚拟地址窗口。默认 RegionConfig 即可；重复调用只生效第一次。
bool Init(const char* argv0 = nullptr);

// 只释放进程内资源，不 shm_unlink：读端可能晚于写端启动。
void Shutdown();

class Node {
 public:
  ~Node() = default;

  const std::string& Name() const { return name_; }

  std::shared_ptr<shm::Segment> GetSegment(const std::string& channel,
                                           const shm::SegmentConfig& cfg = {});

  template <typename MessageT>
  std::shared_ptr<shm::Writer<MessageT>> CreateWriter(
      const std::string& channel, const shm::SegmentConfig& cfg = {}) {
    auto seg = GetSegment(channel, cfg);
    if (!seg) return nullptr;
    return std::make_shared<shm::Writer<MessageT>>(seg);
  }

  template <typename MessageT>
  std::shared_ptr<shm::Reader<MessageT>> CreateReader(
      const std::string& channel, const shm::SegmentConfig& cfg = {}) {
    auto seg = GetSegment(channel, cfg);
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
