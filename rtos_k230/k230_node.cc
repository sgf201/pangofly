#include "k230_node.h"

namespace pangofly {

K230Node::K230Node(const std::string& node_name, const std::string& name_space)
    : node_name_(node_name), name_space_(name_space) {}

const std::string& K230Node::Name() const {
  return node_name_;
}

bool K230Node::DeleteReader(const std::string& channel_name) {
  std::lock_guard<std::mutex> lg(readers_mutex_);
  auto it = readers_.find(channel_name);
  if (it != readers_.end()) {
    readers_.erase(it);
    return true;
  }
  return false;
}

void K230Node::Observe() {
}

void K230Node::ClearData() {
  std::lock_guard<std::mutex> lg(readers_mutex_);
  readers_.clear();
  
  std::lock_guard<std::mutex> lg2(writers_mutex_);
  writers_.clear();
}

std::unique_ptr<K230Node> CreateK230Node(const std::string& node_name, const std::string& name_space) {
  return std::unique_ptr<K230Node>(new K230Node(node_name, name_space));
}

} // namespace pangofly