#include "core/transport/shm/state.h"

namespace pangofly {
namespace transport {

State::State(uint32_t ceiling_msg_size) : ceiling_msg_size_(ceiling_msg_size), reference_count_(0) {
  for (uint32_t i = 0; i < 64; ++i) {
    sequences_[i].store(0);
  }
}

uint32_t State::GetSequence(uint16_t block_index) const {
  if (block_index < 64) {
    return sequences_[block_index].load();
  }
  return 0;
}

void State::SetSequence(uint16_t block_index, uint32_t sequence) {
  if (block_index < 64) {
    sequences_[block_index].store(sequence);
  }
}

} // namespace transport
} // namespace pangofly