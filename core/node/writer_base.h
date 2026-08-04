#ifndef PANGOFLY_NODE_WRITER_BASE_H_
#define PANGOFLY_NODE_WRITER_BASE_H_

#include <memory>
#include <string>
#include <cstring>
#include <unordered_map>

#include "core/node/writer.h"

#include "core/transport/shm/posix_segment.h"
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
    size_t expected = message.CalculateSize() + 1024;
    Sample<MessageT> sample = LoanSample(expected);
    if (!sample.IsValid()) {
      return false;
    }
    *sample.message = message;
    return Write(sample);
  }

  bool Write(const MessageT& message, const MessageInfo& message_info) override {
    (void)message_info;
    return Write(message);
  }

  // Loan a sample from shared memory.
  //
  // A BlockAllocator is initialized in the region right after the MessageT
  // object, wrapped by a ShmResource (pmr::memory_resource) and bound as the
  // thread-local CurrentAllocator via ScopedAllocator. As a result the default
  // constructor of MessageT (and any nested PMR containers) will automatically
  // allocate their internal buffers from shared memory without the caller
  // having to pass an allocator explicitly.
  Sample<MessageT> LoanSample(size_t expected_size = 0) override {
    size_t alloc_size = expected_size > 0 ? expected_size : sizeof(MessageT) + 1024;

    transport::WritableBlock block;
    if (!segment_->AcquireBlockToWrite(alloc_size, &block)) {
      return Sample<MessageT>();
    }

    uint8_t* shm_base = reinterpret_cast<uint8_t*>(block.buf);

    BlockAllocator* allocator = new BlockAllocator();
    allocator->initialize_from_buffer(shm_base + sizeof(MessageT),
                                      alloc_size - sizeof(MessageT));

    Sample<MessageT> sample;
    sample.message = reinterpret_cast<MessageT*>(shm_base);
    sample.allocator = allocator;
    sample.shm_block = block.block;
    sample.shm_size = alloc_size;
    sample.shm_resource = std::make_unique<ShmResource>(allocator);
    // Bind the shared memory resource as the current allocator so that the
    // default constructor of MessageT picks it up automatically.
    sample.scope = std::make_unique<ScopedAllocator>(sample.resource());

    new (sample.message) MessageT();

    loaned_blocks_[sample.message] = block;

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

    // Destroy the message (and its PMR containers) while the scope is still
    // active, so any allocator-dependent cleanup sees the correct resource.
    if (sample.message) {
      sample.message->~MessageT();
    }

    sample.scope.reset();
    sample.shm_resource.reset();

    if (block.block != nullptr) {
      block.block->set_msg_size(sample.shm_size);
    }

    segment_->ReleaseWrittenBlock(block);
    loaned_blocks_.erase(it);

    delete sample.allocator;
    sample.allocator = nullptr;
    sample.message = nullptr;
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

    if (sample.message) {
      sample.message->~MessageT();
    }

    sample.scope.reset();
    sample.shm_resource.reset();

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
  std::string channel_name_;
  std::shared_ptr<PANGOFLY_SEGMENT_TYPE> segment_;
  std::unordered_map<MessageT*, transport::WritableBlock> loaned_blocks_;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_BASE_H_
