#ifndef PANGOFLY_RTOS_K230_READER_WRITER_H_
#define PANGOFLY_RTOS_K230_READER_WRITER_H_

#include "pangofly/node/reader.h"
#include "pangofly/node/writer.h"
#include "k230_segment.h"

namespace pangofly {

class K230ReaderBase {
public:
  virtual ~K230ReaderBase() = default;
  virtual const std::string& ChannelName() const = 0;
};

class K230WriterBase {
public:
  virtual ~K230WriterBase() = default;
  virtual const std::string& ChannelName() const = 0;
};

template <typename MessageT>
class K230ShmReader : public Reader<MessageT>, public K230ReaderBase {
public:
  explicit K230ShmReader(const std::string& channel_name) 
      : channel_name_(channel_name),
        segment_(std::make_shared<transport::K230Segment>(
            std::hash<std::string>()(channel_name), channel_name)) {
    bool open_only = false;
    segment_->OpenOrCreate(open_only);
  }

  ~K230ShmReader() override = default;

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

  const std::string& ChannelName() const override {
    return channel_name_;
  }

private:
  std::string channel_name_;
  std::shared_ptr<transport::K230Segment> segment_;
  CallbackFunc<MessageT> callback_;
};

template <typename MessageT>
class K230ShmWriter : public Writer<MessageT>, public K230WriterBase {
public:
  explicit K230ShmWriter(const std::string& channel_name) 
      : channel_name_(channel_name),
        segment_(std::make_shared<transport::K230Segment>(
            std::hash<std::string>()(channel_name), channel_name)) {
    bool open_only = false;
    segment_->OpenOrCreate(open_only);
  }

  ~K230ShmWriter() override = default;

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
  std::shared_ptr<transport::K230Segment> segment_;
};

} // namespace pangofly

#endif // PANGOFLY_RTOS_K230_READER_WRITER_H_