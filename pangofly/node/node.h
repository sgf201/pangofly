#ifndef PANGOFLY_NODE_NODE_H_
#define PANGOFLY_NODE_NODE_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "pangofly/node/reader.h"
#include "pangofly/node/reader_base.h"
#include "pangofly/node/writer.h"
#include "pangofly/node/writer_base.h"

namespace pangofly {

class Node {
public:
  virtual ~Node() = default;

  const std::string& Name() const;

  template <typename MessageT>
  auto CreateWriter(const std::string& channel_name) -> std::shared_ptr<Writer<MessageT>>;

  template <typename MessageT>
  auto CreateReader(const ReaderConfig& config, const CallbackFunc<MessageT>& reader_func = nullptr)
      -> std::shared_ptr<Reader<MessageT>>;

  template <typename MessageT>
  auto CreateReader(const std::string& channel_name, const CallbackFunc<MessageT>& reader_func = nullptr)
      -> std::shared_ptr<Reader<MessageT>>;

  bool DeleteReader(const std::string& channel_name);

  void Observe();
  void ClearData();

  template <typename MessageT>
  auto GetReader(const std::string& channel_name) -> std::shared_ptr<Reader<MessageT>>;

private:
  explicit Node(const std::string& node_name, const std::string& name_space = "");

  friend std::unique_ptr<Node> CreateNode(const std::string&, const std::string&);

private:
  std::string node_name_;
  std::string name_space_;

  std::mutex readers_mutex_;
  std::map<std::string, std::shared_ptr<ReaderBase>> readers_;

  std::mutex writers_mutex_;
  std::map<std::string, std::shared_ptr<WriterBase>> writers_;
};

template <typename MessageT>
auto Node::CreateWriter(const std::string& channel_name) -> std::shared_ptr<Writer<MessageT>> {
  std::lock_guard<std::mutex> lg(writers_mutex_);
  auto it = writers_.find(channel_name);
  if (it != writers_.end()) {
    return std::dynamic_pointer_cast<Writer<MessageT>>(it->second);
  }

  auto writer = std::make_shared<ShmWriter<MessageT>>(channel_name);
  writers_.emplace(channel_name, writer);
  return writer;
}

template <typename MessageT>
auto Node::CreateReader(const ReaderConfig& config, const CallbackFunc<MessageT>& reader_func)
    -> std::shared_ptr<Reader<MessageT>> {
  return CreateReader<MessageT>(config.channel_name, reader_func);
}

template <typename MessageT>
auto Node::CreateReader(const std::string& channel_name, const CallbackFunc<MessageT>& reader_func)
    -> std::shared_ptr<Reader<MessageT>> {
  std::lock_guard<std::mutex> lg(readers_mutex_);
  if (readers_.find(channel_name) != readers_.end()) {
    return nullptr;
  }

  auto reader = std::make_shared<ShmReader<MessageT>>(channel_name);
  if (reader_func) {
    reader->SetCallback(reader_func);
  }
  readers_.emplace(channel_name, reader);
  return reader;
}

template <typename MessageT>
auto Node::GetReader(const std::string& channel_name) -> std::shared_ptr<Reader<MessageT>> {
  std::lock_guard<std::mutex> lg(readers_mutex_);
  auto it = readers_.find(channel_name);
  if (it != readers_.end()) {
    return std::dynamic_pointer_cast<Reader<MessageT>>(it->second);
  }
  return nullptr;
}

} // namespace pangofly

#endif // PANGOFLY_NODE_NODE_H_