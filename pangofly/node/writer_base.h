#ifndef PANGOFLY_NODE_WRITER_BASE_H_
#define PANGOFLY_NODE_WRITER_BASE_H_

#include <memory>
#include <string>
#include <cstring>

#include "pangofly/node/writer.h"

#include "pangofly/transport/shm/posix_segment.h"
#include "idl/container/vector.h"
#define PANGOFLY_SEGMENT_TYPE transport::PosixSegment

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
        segment_(std::make_shared<PANGOFLY_SEGMENT_TYPE>(
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
    size_t total_size = sizeof(MessageT);
    
    const auto* msg_ptr = &message;
    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(msg_ptr);
    size_t struct_size = sizeof(MessageT);
    
    for (size_t i = 0; i < struct_size; i += sizeof(uintptr_t)) {
        uintptr_t maybe_ptr = *reinterpret_cast<const uintptr_t*>(data_ptr + i);
        if (maybe_ptr != 0) {
            const Vector<uint8_t>* vec = reinterpret_cast<const Vector<uint8_t>*>(data_ptr + i);
            total_size += vec->size() * sizeof(uint8_t);
        }
    }
    
    transport::WritableBlock block;
    if (!segment_->AcquireBlockToWrite(total_size, &block)) {
      return false;
    }

    new (block.buf) MessageT(message);
    
    if (block.block != nullptr) {
      block.block->set_msg_size(total_size);
    }
    
    segment_->ReleaseWrittenBlock(block);
    return true;
  }

  const std::string& ChannelName() const override {
    return channel_name_;
  }

private:
  std::string channel_name_;
  std::shared_ptr<PANGOFLY_SEGMENT_TYPE> segment_;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_BASE_H_