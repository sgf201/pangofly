#ifndef PANGOFLY_NODE_WRITER_H_
#define PANGOFLY_NODE_WRITER_H_

#include <memory>
#include <string>

#include "pangofly/message/message_info.h"

namespace pangofly {

template <typename MessageT>
class Writer {
public:
  virtual ~Writer() = default;
  
  virtual bool Write(const MessageT& message) = 0;
  virtual bool Write(const MessageT& message, const MessageInfo& message_info) = 0;
  
  virtual const std::string& ChannelName() const = 0;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_H_