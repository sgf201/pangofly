#include "k230_segment.h"

#include <stdio.h>
#include <string.h>

#include <lwp_shm.h>

#include "pangofly/common/log.h"

namespace pangofly {
namespace transport {

static const size_t SHM_KEY_BASE = 0x80000000;

static size_t shm_name_to_key(const std::string& name) {
    size_t key = SHM_KEY_BASE;
    for (char c : name) {
        key = key * 31 + static_cast<size_t>(c);
    }
    return key;
}

K230Segment::K230Segment(uint64_t channel_id, std::string channel_name)
    : Segment(channel_id, channel_name) {
  shm_name_ = shm_prefix_ + std::to_string(channel_id);
}

K230Segment::K230Segment(std::string prefix, uint64_t channel_id, size_t size)
    : Segment(channel_id, size) {
  shm_name_ = prefix + std::to_string(channel_id);
}

K230Segment::~K230Segment() { K230Segment::Destroy(); }

bool K230Segment::OpenOrCreate(bool& open_only) {
  if (init_) {
    return true;
  }

  size_t key = shm_name_to_key(shm_name_);
  
  int shm_id = lwp_shmget(key, conf_.managed_shm_size(), 1);
  if (shm_id < 0) {
    PANGOFLY_ERROR << "lwp_shmget failed: " << shm_name_ << std::endl;
    return false;
  }

  shm_id_ = shm_id;
  
  managed_shm_ = lwp_shmat(shm_id_, nullptr);
  if (!managed_shm_) {
    PANGOFLY_ERROR << "lwp_shmat failed" << std::endl;
    lwp_shmrm(shm_id_);
    return false;
  }

  PANGOFLY_INFO << "Successfully mapped shm: " << managed_shm_ << std::endl;

  state_ = new (managed_shm_) State(conf_.ceiling_msg_size());
  if (state_ == nullptr) {
    PANGOFLY_ERROR << "create state failed." << std::endl;
    lwp_shmdt(managed_shm_);
    managed_shm_ = nullptr;
    lwp_shmrm(shm_id_);
    return false;
  }

  conf_.Update(state_->ceiling_msg_size());

  blocks_ = reinterpret_cast<Block*>(static_cast<char*>(managed_shm_) + sizeof(State));
  if (blocks_ == nullptr) {
    PANGOFLY_ERROR << "create blocks failed." << std::endl;
    state_->~State();
    state_ = nullptr;
    lwp_shmdt(managed_shm_);
    managed_shm_ = nullptr;
    lwp_shmrm(shm_id_);
    return false;
  }

  for (uint32_t i = 0; i < conf_.block_num(); ++i) {
    uint8_t* addr = reinterpret_cast<uint8_t*>(
        static_cast<char*>(managed_shm_) + sizeof(State) +
        conf_.block_num() * sizeof(Block) + i * conf_.block_buf_size());
    block_buf_addrs_[i] = addr;
  }

  state_->IncreaseReferenceCounts();
  init_ = true;
  PANGOFLY_INFO << "create shm, channel: " << channel_name_ << ", shm name: " << shm_name_
                << ", ceiling size: " << conf_.ceiling_msg_size() << std::endl;
  return true;
}

bool K230Segment::OpenOnly() {
  if (init_) {
    return true;
  }

  size_t key = shm_name_to_key(shm_name_);
  int shm_id = lwp_shmget(key, 0, 0);
  
  if (shm_id < 0) {
    PANGOFLY_ERROR << "get shm failed" << std::endl;
    return false;
  }

  shm_id_ = shm_id;
  
  managed_shm_ = lwp_shmat(shm_id_, nullptr);
  if (!managed_shm_) {
    PANGOFLY_ERROR << "lwp_shmat failed" << std::endl;
    return false;
  }

  PANGOFLY_INFO << "Successfully mapped existing shm: " << managed_shm_ << std::endl;

  state_ = reinterpret_cast<State*>(managed_shm_);
  if (state_ == nullptr) {
    PANGOFLY_ERROR << "get state failed." << std::endl;
    lwp_shmdt(managed_shm_);
    managed_shm_ = nullptr;
    return false;
  }

  conf_.Update(state_->ceiling_msg_size());

  blocks_ = reinterpret_cast<Block*>(static_cast<char*>(managed_shm_) + sizeof(State));
  if (blocks_ == nullptr) {
    PANGOFLY_ERROR << "get blocks failed." << std::endl;
    state_ = nullptr;
    lwp_shmdt(managed_shm_);
    managed_shm_ = nullptr;
    return false;
  }

  for (uint32_t i = 0; i < conf_.block_num(); ++i) {
    uint8_t* addr = reinterpret_cast<uint8_t*>(
        static_cast<char*>(managed_shm_) + sizeof(State) +
        conf_.block_num() * sizeof(Block) + i * conf_.block_buf_size());
    block_buf_addrs_[i] = addr;
  }

  state_->IncreaseReferenceCounts();
  init_ = true;
  return true;
}

bool K230Segment::Remove() {
  if (shm_id_ >= 0) {
    if (lwp_shmrm(shm_id_) < 0) {
      PANGOFLY_ERROR << "shm remove failed" << std::endl;
      return false;
    }
    shm_id_ = -1;
  }
  return true;
}

void K230Segment::Reset() {
  state_ = nullptr;
  blocks_ = nullptr;
  block_buf_addrs_.clear();
  if (managed_shm_ != nullptr) {
    lwp_shmdt(managed_shm_);
    managed_shm_ = nullptr;
  }
}

} // namespace transport
} // namespace pangofly