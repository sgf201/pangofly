#ifndef PANGOFLY_NODE_WRITER_H_
#define PANGOFLY_NODE_WRITER_H_

#include <memory>
#include <string>

#include "core/message/message_info.h"
#include "idl/allocator/block_allocator.h"
#include "core/memory/allocator.h"
#include "core/memory/shm_resource.h"

namespace pangofly {

template <typename MessageT>
struct Sample {
  MessageT* message;
  BlockAllocator* allocator;
  void* shm_block;
  size_t shm_size;
  std::unique_ptr<ShmResource> shm_resource;
  std::unique_ptr<ScopedAllocator> scope;

  Sample() : message(nullptr), allocator(nullptr), shm_block(nullptr), shm_size(0) {}

  Sample(MessageT* msg, BlockAllocator* alloc, void* block, size_t size)
      : message(msg), allocator(alloc), shm_block(block), shm_size(size) {}

  bool IsValid() const { return message != nullptr && shm_block != nullptr; }

  pangofly::pmr::memory_resource* resource() {
    return shm_resource ? shm_resource.get() : pangofly::pmr::get_default_resource();
  }
};

template <typename MessageT>
class Writer {
public:
  virtual ~Writer() = default;

  virtual bool Write(const MessageT& message) = 0;
  virtual bool Write(const MessageT& message, const MessageInfo& message_info) = 0;

  // Loan a sample from shared memory. The returned Sample has a ScopedAllocator
  // that binds the shared memory resource as the current allocator, so any PMR
  // containers constructed afterwards will automatically allocate from shared memory.
  virtual Sample<MessageT> LoanSample(size_t expected_size = 0) = 0;
  virtual bool Write(Sample<MessageT>& sample) = 0;
  virtual void Release(Sample<MessageT>& sample) = 0;

  virtual const std::string& ChannelName() const = 0;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_H_