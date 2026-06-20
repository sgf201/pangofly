#ifndef PANGOFLY_NODE_READER_H_
#define PANGOFLY_NODE_READER_H_

#include <functional>
#include <memory>
#include <string>

#include "pangofly/message/message_info.h"

namespace pangofly {

template <typename MessageT>
using CallbackFunc = std::function<void(const MessageT&, const MessageInfo&)>;

struct ReaderConfig {
  std::string channel_name;
  std::string type_name;
  uint32_t pending_queue_size = 10;
};

template <typename MessageT>
class Reader {
public:
  virtual ~Reader() = default;

  virtual bool Init() = 0;
  virtual void SetCallback(const CallbackFunc<MessageT>& callback) = 0;
  virtual bool Read(MessageT* message, MessageInfo* message_info) = 0;

  virtual const MessageT* ReadLatest() = 0;
  virtual void ReleaseLatest() = 0;

  virtual const std::string& ChannelName() const = 0;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_READER_H_