#include "pangofly/transport/shm/segment.h"

#include <atomic>

#include "pangofly/common/log.h"

namespace pangofly {
namespace transport {

std::string Segment::shm_prefix_ = "/pangofly_shm_";

Segment::Segment(uint64_t channel_id, std::string channel_name)
    : conf_(channel_name), channel_id_(channel_id), channel_name_(channel_name) {}

Segment::Segment(uint64_t channel_id, std::size_t min_size)
    : conf_(static_cast<uint32_t>(min_size), channel_id), channel_id_(channel_id) {}

bool Segment::OpenOrCreate() {
  bool open_only = false;
  return OpenOrCreate(open_only);
}

bool Segment::AcquireBlockToWrite(std::size_t msg_size, WritableBlock* writable_block) {
  if (!init_) {
    return false;
  }

  uint16_t index = GetNextWritableBlockIndex();
  if (index >= conf_.block_num()) {
    return false;
  }

  writable_block->index = index;
  writable_block->msg_type = 0;
  writable_block->ceiling_msg_size = conf_.ceiling_msg_size();
  writable_block->block = &blocks_[index];
  writable_block->buf = block_buf_addrs_[index];

  return true;
}

void Segment::ReleaseWrittenBlock(const WritableBlock& writable_block) {
  if (writable_block.block != nullptr) {
    uint32_t old_seq = state_->GetSequence(writable_block.index);
    writable_block.block->set_sequence(old_seq + 1);
    state_->SetSequence(writable_block.index, writable_block.block->sequence());
    std::atomic_thread_fence(std::memory_order_release);
  }
}

bool Segment::AcquireBlockToRead(ReadableBlock* readable_block) {
  if (!init_) {
    return false;
  }

  std::atomic_thread_fence(std::memory_order_acquire);

  for (uint32_t i = 0; i < conf_.block_num(); ++i) {
    if (blocks_[i].sequence() > 0) {
      readable_block->index = i;
      readable_block->msg_type = 0;
      readable_block->ceiling_msg_size = conf_.ceiling_msg_size();
      readable_block->block = &blocks_[i];
      readable_block->buf = block_buf_addrs_[i];
      return true;
    }
  }
  return false;
}

bool Segment::AcquireFixedBlock(std::size_t msg_size, WritableBlock* writable_block, bool& open_only) {
  if (!init_ && !OpenOrCreate(open_only)) {
    return false;
  }

  writable_block->index = 0;
  writable_block->msg_type = 0;
  writable_block->ceiling_msg_size = conf_.ceiling_msg_size();
  writable_block->block = blocks_;
  writable_block->buf = block_buf_addrs_[0];

  return true;
}

void Segment::ReleaseReadBlock(const ReadableBlock& readable_block) {
  if (readable_block.block != nullptr) {
    state_->SetSequence(readable_block.index, 0);
    readable_block.block->set_sequence(0);
    std::atomic_thread_fence(std::memory_order_release);
  }
}

bool Segment::HasOwnerShip(char* ptr) {
  if (!init_) {
    return false;
  }
  uint8_t* start = reinterpret_cast<uint8_t*>(managed_shm_);
  uint8_t* end = start + conf_.managed_shm_size();
  return (ptr >= reinterpret_cast<char*>(start)) && (ptr < reinterpret_cast<char*>(end));
}

std::recursive_mutex& Segment::GetSegmentLock() {
  return segment_lock_;
}

bool Segment::Destroy() {
  Reset();
  return Remove();
}

bool Segment::Remap() {
  Reset();
  return OpenOrCreate();
}

bool Segment::Recreate(const uint64_t& msg_size) {
  Remove();
  Reset();
  conf_.Update(static_cast<uint32_t>(msg_size));
  bool open_only = false;
  return OpenOrCreate(open_only);
}

uint16_t Segment::GetNextWritableBlockIndex() {
  static uint16_t next_index = 0;
  uint16_t current = next_index;
  next_index = (next_index + 1) % conf_.block_num();
  return current;
}

} // namespace transport
} // namespace pangofly