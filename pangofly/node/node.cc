#include "pangofly/node/node.h"

namespace pangofly {

Node::Node(const std::string& node_name, const std::string& name_space)
    : node_name_(node_name), name_space_(name_space) {}

const std::string& Node::Name() const {
  return node_name_;
}

bool Node::DeleteReader(const std::string& channel_name) {
  std::lock_guard<std::mutex> lg(readers_mutex_);
  auto it = readers_.find(channel_name);
  if (it != readers_.end()) {
    readers_.erase(it);
    return true;
  }
  return false;
}

void Node::Observe() {
}

void Node::ClearData() {
}

std::unique_ptr<Node> CreateNode(const std::string& node_name, const std::string& name_space) {
  return std::unique_ptr<Node>(new Node(node_name, name_space));
}

} // namespace pangofly