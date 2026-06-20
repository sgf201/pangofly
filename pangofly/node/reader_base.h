#ifndef PANGOFLY_NODE_READER_BASE_H_
#define PANGOFLY_NODE_READER_BASE_H_

#include <memory>
#include <string>
#include <functional>

#include "pangofly/node/reader.h"

#include "pangofly/transport/shm/posix_segment.h"
#define PANGOFLY_SEGMENT_TYPE transport::PosixSegment

namespace pangofly {

class ReaderBase {
public:
  virtual ~ReaderBase() = default;
  virtual const std::string& ChannelName() const = 0;
  virtual void Observe() {}
};

template <typename MessageT>
class ShmReader : public Reader<MessageT>, public ReaderBase {
public:
  using MessagePtr = std::unique_ptr<MessageT, std::function<void(MessageT*)>>;

  explicit ShmReader(const std::string& channel_name) 
      : channel_name_(channel_name),
        segment_(std::make_shared<PANGOFLY_SEGMENT_TYPE>(
            std::hash<std::string>()(channel_name), channel_name)) {
    bool open_only = false;
    segment_->OpenOrCreate(open_only);
  }

  ~ShmReader() override = default;

  bool Init() override {
    return true;
  }

  void SetCallback(const CallbackFunc<MessageT>& callback) override {
    callback_ = callback;
  }

  bool Read(MessageT* message, MessageInfo* message_info) override {
    transport::ReadableBlock block;
    if (!segment_->AcquireBlockToRead(&block)) {
      return false;
    }

    if (block.buf != nullptr && block.block != nullptr) {
      *message = *reinterpret_cast<MessageT*>(block.buf);
      if (message_info != nullptr) {
        message_info->seq = block.block->sequence();
      }
    }

    segment_->ReleaseReadBlock(block);
    return true;
  }

  MessagePtr ReadWithAllocator() {
    transport::ReadableBlock block;
    if (!segment_->AcquireBlockToRead(&block)) {
      return nullptr;
    }

    if (block.buf == nullptr || block.block == nullptr) {
      segment_->ReleaseReadBlock(block);
      return nullptr;
    }

    MessageT* msg = reinterpret_cast<MessageT*>(block.buf);
    
    auto deleter = [this, block](MessageT*) mutable {
      segment_->ReleaseReadBlock(block);
    };

    return MessagePtr(msg, deleter);
  }

  void Observe() override {
    if (!callback_) {
      return;
    }
    
    transport::ReadableBlock block;
    if (!segment_->AcquireBlockToRead(&block)) {
      return;
    }

    if (block.buf != nullptr && block.block != nullptr) {
      const MessageT& message = *reinterpret_cast<const MessageT*>(block.buf);
      MessageInfo info;
      info.seq = block.block->sequence();
      callback_(message, info);
    }

    segment_->ReleaseReadBlock(block);
  }

  void ObserveWithAllocator() {
    if (!callback_) {
      return;
    }

    MessagePtr msg = ReadWithAllocator();
    if (!msg) {
      return;
    }

    MessageInfo info;
    info.seq = 0;
    callback_(*msg, info);
  }

  const std::string& ChannelName() const override {
    return channel_name_;
  }

private:
  std::string channel_name_;
  std::shared_ptr<PANGOFLY_SEGMENT_TYPE> segment_;
  CallbackFunc<MessageT> callback_;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_READER_BASE_H_