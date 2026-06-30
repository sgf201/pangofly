#ifndef PANGOFLY_NODE_WRITER_BASE_H_
#define PANGOFLY_NODE_WRITER_BASE_H_

#include <memory>
#include <string>
#include <cstring>

#include "core/node/writer.h"

#include "core/transport/shm/posix_segment.h"
#include "idl/container/vector.h"
#include "idl/allocator/block_allocator.h"
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
    size_t total_size = calculate_message_size(message);

    transport::WritableBlock block;
    if (!segment_->AcquireBlockToWrite(total_size, &block)) {
      return false;
    }

    uint8_t* shm_base = reinterpret_cast<uint8_t*>(block.buf);
    size_t offset = 0;

    offset = serialize_to_shm(message, shm_base, offset);

    if (block.block != nullptr) {
      block.block->set_msg_size(total_size);
    }

    segment_->ReleaseWrittenBlock(block);
    return true;
  }

  Sample<MessageT> LoanSample(size_t expected_size = 0) override {
    size_t alloc_size = expected_size > 0 ? expected_size : sizeof(MessageT) + 1024;

    transport::WritableBlock block;
    if (!segment_->AcquireBlockToWrite(alloc_size, &block)) {
      return Sample<MessageT>();
    }

    uint8_t* shm_base = reinterpret_cast<uint8_t*>(block.buf);

    BlockAllocator* allocator = new BlockAllocator();
    allocator->initialize_from_buffer(shm_base + sizeof(MessageT), alloc_size - sizeof(MessageT));

    MessageT* message = reinterpret_cast<MessageT*>(shm_base);
    new (message) MessageT(allocator);

    Sample<MessageT> sample(message, allocator, block.block, alloc_size);
    loaned_blocks_[message] = block;

    return sample;
  }

  bool Write(Sample<MessageT>& sample) override {
    if (!sample.IsValid()) {
      return false;
    }

    auto it = loaned_blocks_.find(sample.message);
    if (it == loaned_blocks_.end()) {
      return false;
    }

    transport::WritableBlock& block = it->second;

    size_t actual_size = sample.message->CalculateSize();

    if (block.block != nullptr) {
      block.block->set_msg_size(actual_size);
    }

    segment_->ReleaseWrittenBlock(block);
    loaned_blocks_.erase(it);

    delete sample.allocator;
    sample.allocator = nullptr;
    sample.shm_block = nullptr;

    return true;
  }

  void Release(Sample<MessageT>& sample) override {
    if (!sample.IsValid()) {
      return;
    }

    auto it = loaned_blocks_.find(sample.message);
    if (it != loaned_blocks_.end()) {
      segment_->ReleaseWrittenBlock(it->second);
      loaned_blocks_.erase(it);
    }

    if (sample.allocator) {
      delete sample.allocator;
      sample.allocator = nullptr;
    }

    sample.message = nullptr;
    sample.shm_block = nullptr;
  }

  const std::string& ChannelName() const override {
    return channel_name_;
  }

private:
  size_t calculate_message_size(const MessageT& message) {
    return message.CalculateSize();
  }

  size_t serialize_to_shm(const MessageT& message, uint8_t* shm_base, size_t offset) {
    const uint8_t* msg_ptr = reinterpret_cast<const uint8_t*>(&message);
    size_t struct_size = sizeof(MessageT);

    memcpy(shm_base, msg_ptr, struct_size);
    offset = struct_size;

    for (size_t i = 0; i < struct_size; i += sizeof(uintptr_t)) {
      uintptr_t vec_ptr = *reinterpret_cast<const uintptr_t*>(msg_ptr + i);
      if (vec_ptr != 0) {
        const Vector<uint8_t>* vec = reinterpret_cast<const Vector<uint8_t>*>(msg_ptr + i);
        size_t data_size = vec->size();

        Vector<uint8_t>* shm_vec = reinterpret_cast<Vector<uint8_t>*>(shm_base + i);
        shm_vec->~Vector();
        new (shm_vec) Vector<uint8_t>();

        if (data_size > 0) {
          memcpy(shm_base + offset, vec->data(), data_size);
          uint8_t* data_ptr = shm_base + offset;
          new (shm_vec) Vector<uint8_t>(data_ptr, data_size);
          offset += data_size;
        } else {
          new (shm_vec) Vector<uint8_t>();
        }
      }
    }

    return offset;
  }

  std::string channel_name_;
  std::shared_ptr<PANGOFLY_SEGMENT_TYPE> segment_;
  std::unordered_map<MessageT*, transport::WritableBlock> loaned_blocks_;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_BASE_H_