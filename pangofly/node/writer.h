#ifndef PANGOFLY_NODE_WRITER_H_
#define PANGOFLY_NODE_WRITER_H_

#include <memory>
#include <string>

#include "pangofly/message/message_info.h"
#include "idl/allocator/block_allocator.h"

namespace pangofly {

template <typename MessageT>
struct Sample {
  MessageT* message;
  BlockAllocator* allocator;
  void* shm_block;
  size_t shm_size;

  Sample() : message(nullptr), allocator(nullptr), shm_block(nullptr), shm_size(0) {}

  Sample(MessageT* msg, BlockAllocator* alloc, void* block, size_t size)
      : message(msg), allocator(alloc), shm_block(block), shm_size(size) {}

  bool IsValid() const { return message != nullptr && shm_block != nullptr; }
};

template <typename MessageT>
class Writer {
public:
  virtual ~Writer() = default;

  virtual bool Write(const MessageT& message) = 0;
  virtual bool Write(const MessageT& message, const MessageInfo& message_info) = 0;

  virtual Sample<MessageT> LoanSample(size_t expected_size = 0) = 0;
  virtual bool Write(Sample<MessageT>& sample) = 0;
  virtual void Release(Sample<MessageT>& sample) = 0;

  virtual const std::string& ChannelName() const = 0;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_H_