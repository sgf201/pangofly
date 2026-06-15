#include "pangofly/transport/shm/k230_shm.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "pangofly/common/log.h"
#include "pangofly/transport/shm/shm_region.h"

extern "C" {
static inline long syscall0(int num) {
    register long a7 __asm__("a7") = num;
    register long a0 __asm__("a0");
    __asm__ __volatile__ ("ecall" : "=r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long syscall1(int num, long arg1) {
    register long a7 __asm__("a7") = num;
    register long a0 __asm__("a0") = arg1;
    __asm__ __volatile__ ("ecall" : "=r"(a0) : "r"(a0), "r"(a7) : "memory");
    return a0;
}

static inline long syscall2(int num, long arg1, long arg2) {
    register long a7 __asm__("a7") = num;
    register long a0 __asm__("a0") = arg1;
    register long a1 __asm__("a1") = arg2;
    __asm__ __volatile__ ("ecall" : "=r"(a0) : "r"(a0), "r"(a1), "r"(a7) : "memory");
    return a0;
}

static inline long syscall3(int num, long arg1, long arg2, long arg3) {
    register long a7 __asm__("a7") = num;
    register long a0 __asm__("a0") = arg1;
    register long a1 __asm__("a1") = arg2;
    register long a2 __asm__("a2") = arg3;
    __asm__ __volatile__ ("ecall" : "=r"(a0) : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}

#define _NRSYS_shmget 55
#define _NRSYS_shmrm  56
#define _NRSYS_shmat  57
#define _NRSYS_shmdt  58

static int lwp_shmget(size_t key, size_t size, int create) {
    return static_cast<int>(syscall3(_NRSYS_shmget, key, size, create));
}

static int lwp_shmrm(int id) {
    return static_cast<int>(syscall1(_NRSYS_shmrm, id));
}

static void* lwp_shmat(int id, void* shm_vaddr) {
    return reinterpret_cast<void*>(syscall2(_NRSYS_shmat, id, reinterpret_cast<long>(shm_vaddr)));
}

static int lwp_shmdt(void* shm_vaddr) {
    return static_cast<int>(syscall1(_NRSYS_shmdt, reinterpret_cast<long>(shm_vaddr)));
}
}

namespace pangofly {
namespace transport {

static const size_t SHM_KEY_BASE = 0x80000000;
static const uint64_t FIXED_VADDR_STEP = 0x200000ULL;

static size_t shm_name_to_key(const std::string& name) {
    size_t key = SHM_KEY_BASE;
    for (char c : name) {
        key = key * 31 + static_cast<size_t>(c);
    }
    return key;
}

static uint64_t get_fixed_address(uint64_t channel_id) {
    uint64_t base = ShmRegion::GetReserveAddr();
    return base + (channel_id % 100) * FIXED_VADDR_STEP;
}

K230Shm::K230Shm(uint64_t channel_id, std::string channel_name)
    : Segment(channel_id, channel_name) {
  shm_name_ = shm_prefix_ + std::to_string(channel_id);
  fixed_address_ = reinterpret_cast<void*>(get_fixed_address(channel_id));
}

K230Shm::K230Shm(std::string prefix, uint64_t channel_id, size_t size)
    : Segment(channel_id, size) {
  shm_name_ = prefix + std::to_string(channel_id);
  fixed_address_ = reinterpret_cast<void*>(get_fixed_address(channel_id));
}

K230Shm::~K230Shm() { K230Shm::Destroy(); }

bool K230Shm::OpenOrCreate(bool& open_only) {
  if (init_) {
    return true;
  }

  size_t key = shm_name_to_key(shm_name_);
  PANGOFLY_INFO << "Creating shm with key: " << std::hex << key << std::dec 
                << ", size: " << conf_.managed_shm_size() 
                << ", fixed_addr: " << fixed_address_ << std::endl;
  
  int shm_id = lwp_shmget(key, conf_.managed_shm_size(), 1);
  if (shm_id < 0) {
    PANGOFLY_ERROR << "lwp_shmget failed" << std::endl;
    return false;
  }

  shm_id_ = shm_id;
  
  managed_shm_ = lwp_shmat(shm_id_, fixed_address_);
  if (!managed_shm_) {
    PANGOFLY_ERROR << "lwp_shmat with fixed address failed, trying without fixed address" << std::endl;
    managed_shm_ = lwp_shmat(shm_id_, nullptr);
    if (!managed_shm_) {
      PANGOFLY_ERROR << "lwp_shmat failed" << std::endl;
      lwp_shmrm(shm_id_);
      return false;
    }
    PANGOFLY_WARN << "Using fallback address: " << managed_shm_ << std::endl;
  } else {
    PANGOFLY_INFO << "Successfully mapped shm to fixed address: " << managed_shm_ << std::endl;
  }

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

bool K230Shm::OpenOnly() {
  if (init_) {
    return true;
  }

  size_t key = shm_name_to_key(shm_name_);
  PANGOFLY_INFO << "Opening existing shm with key: " << std::hex << key << std::dec << std::endl;
  
  int shm_id = lwp_shmget(key, 0, 0);
  
  if (shm_id < 0) {
    PANGOFLY_ERROR << "lwp_shmget (open only) failed" << std::endl;
    return false;
  }

  shm_id_ = shm_id;
  
  managed_shm_ = lwp_shmat(shm_id_, fixed_address_);
  if (!managed_shm_) {
    PANGOFLY_WARN << "lwp_shmat with fixed address failed, trying without fixed address" << std::endl;
    managed_shm_ = lwp_shmat(shm_id_, nullptr);
    if (!managed_shm_) {
      PANGOFLY_ERROR << "lwp_shmat (open only) failed" << std::endl;
      return false;
    }
    PANGOFLY_WARN << "Using fallback address: " << managed_shm_ << std::endl;
  } else {
    PANGOFLY_INFO << "Successfully mapped existing shm to fixed address: " << managed_shm_ << std::endl;
  }

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

bool K230Shm::Remove() {
  if (shm_id_ >= 0) {
    if (lwp_shmrm(shm_id_) < 0) {
      PANGOFLY_ERROR << "shm remove failed" << std::endl;
      return false;
    }
    shm_id_ = -1;
  }
  return true;
}

void K230Shm::Reset() {
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