#ifndef PANGOFLY_NODE_WRITER_BASE_H_
#define PANGOFLY_NODE_WRITER_BASE_H_

#include <string>

#include "pangofly/node/writer.h"
#include "pangofly/transport/shm/posix_segment.h"

namespace pangofly {

class WriterBase {
public:
  virtual ~WriterBase() = default;
  virtual const std::string& ChannelName() const = 0;
};

template <typename MessageT>
class ShmWriter : public Writer<MessageT>, public WriterBase {
public:
  explicit ShmWriter(const std::string& channel_name) 
      : channel_name_(channel_name),
        segment_(std::make_shared<transport::PosixSegment>(
            std::hash<std::string>()(channel_name), channel_name)) {
    bool open_only = false;
    segment_->OpenOrCreate(open_only);
  }

  ~ShmWriter() override = default;

  bool Write(const MessageT& message) override {
    MessageInfo info;
    return Write(message, info);
  }

  bool Write(const MessageT& message, const MessageInfo& message_info) override {
    transport::WritableBlock block;
    if (!segment_->AcquireBlockToWrite(sizeof(MessageT), &block)) {
      return false;
    }

    new (block.buf) MessageT(message);
    
    if (block.block != nullptr) {
      block.block->set_msg_size(sizeof(MessageT));
    }
    
    segment_->ReleaseWrittenBlock(block);
    return true;
  }

  const std::string& ChannelName() const override {
    return channel_name_;
  }

private:
  std::string channel_name_;
  std::shared_ptr<transport::PosixSegment> segment_;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_BASE_H_