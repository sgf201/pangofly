#ifndef PANGOFLY_RTOS_K230_NODE_H_
#define PANGOFLY_RTOS_K230_NODE_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "k230_reader_writer.h"

namespace pangofly {

class K230Node {
public:
  virtual ~K230Node() = default;

  const std::string& Name() const;

  template <typename MessageT>
  auto CreateWriter(const std::string& channel_name) -> std::shared_ptr<K230ShmWriter<MessageT>>;

  template <typename MessageT>
  auto CreateReader(const std::string& channel_name, const CallbackFunc<MessageT>& reader_func = nullptr)
      -> std::shared_ptr<K230ShmReader<MessageT>>;

  bool DeleteReader(const std::string& channel_name);

  void Observe();
  void ClearData();

  template <typename MessageT>
  auto GetReader(const std::string& channel_name) -> std::shared_ptr<K230ShmReader<MessageT>>;

private:
  explicit K230Node(const std::string& node_name, const std::string& name_space = "");

  friend std::unique_ptr<K230Node> CreateK230Node(const std::string&, const std::string&);

private:
  std::string node_name_;
  std::string name_space_;

  std::mutex readers_mutex_;
  std::map<std::string, std::shared_ptr<K230ReaderBase>> readers_;

  std::mutex writers_mutex_;
  std::map<std::string, std::shared_ptr<K230WriterBase>> writers_;
};

template <typename MessageT>
auto K230Node::CreateWriter(const std::string& channel_name) -> std::shared_ptr<K230ShmWriter<MessageT>> {
  std::lock_guard<std::mutex> lg(writers_mutex_);
  auto it = writers_.find(channel_name);
  if (it != writers_.end()) {
    return std::dynamic_pointer_cast<K230ShmWriter<MessageT>>(it->second);
  }

  auto writer = std::make_shared<K230ShmWriter<MessageT>>(channel_name);
  writers_.emplace(channel_name, writer);
  return writer;
}

template <typename MessageT>
auto K230Node::CreateReader(const std::string& channel_name, const CallbackFunc<MessageT>& reader_func)
    -> std::shared_ptr<K230ShmReader<MessageT>> {
  std::lock_guard<std::mutex> lg(readers_mutex_);
  if (readers_.find(channel_name) != readers_.end()) {
    return nullptr;
  }

  auto reader = std::make_shared<K230ShmReader<MessageT>>(channel_name);
  if (reader_func) {
    reader->SetCallback(reader_func);
  }
  readers_.emplace(channel_name, reader);
  return reader;
}

template <typename MessageT>
auto K230Node::GetReader(const std::string& channel_name) -> std::shared_ptr<K230ShmReader<MessageT>> {
  std::lock_guard<std::mutex> lg(readers_mutex_);
  auto it = readers_.find(channel_name);
  if (it != readers_.end()) {
    return std::dynamic_pointer_cast<K230ShmReader<MessageT>>(it->second);
  }
  return nullptr;
}

std::unique_ptr<K230Node> CreateK230Node(const std::string& node_name, const std::string& name_space = "");

} // namespace pangofly

#endif // PANGOFLY_RTOS_K230_NODE_H_