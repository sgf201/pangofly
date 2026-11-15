#include "core/pangofly.h"

#include "core/common/log.h"

namespace pangofly {

bool Init(const char* argv0) {
  (void)argv0;
  return shm::Region::Init(shm::RegionConfig{});
}

void Shutdown() {
  // 每个进程独立释放自己的映射；共享内存对象本身保留，
  // 以便读端晚于写端启动时仍能 attach。
}

std::shared_ptr<shm::Segment> Node::GetSegment(const std::string& channel,
                                               const shm::SegmentConfig& cfg,
                                               const char* type_name) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = segments_.find(channel);
  if (it != segments_.end()) {
    // 进程内复用同一个 Segment 时同样要校验类型：否则 CreateWriter<A> 与
    // CreateReader<B> 指向同一 channel 会被静默放行。
    if (it->second->TypeHashValue() != shm::TypeHash(type_name)) {
      PANGOFLY_ERROR << "[node] TYPE MISMATCH on channel=" << channel << ": already open as '"
                     << it->second->TypeName() << "' but requested '" << type_name << "'"
                     << std::endl;
      return nullptr;
    }
    return it->second;
  }

  auto seg = std::make_shared<shm::Segment>();
  if (!seg->OpenOrCreate(channel, cfg, type_name)) {
    PANGOFLY_ERROR << "[node] open channel failed: " << channel << std::endl;
    return nullptr;
  }
  segments_.emplace(channel, seg);
  return seg;
}

std::unique_ptr<Node> CreateNode(const std::string& node_name) {
  return std::unique_ptr<Node>(new Node(node_name));
}

}  // namespace pangofly
