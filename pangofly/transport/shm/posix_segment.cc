#include "pangofly/transport/shm/posix_segment.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

#include "pangofly/common/log.h"

namespace pangofly {
namespace transport {

PosixSegment::PosixSegment(uint64_t channel_id, std::string channel_name)
    : Segment(channel_id, channel_name) {
  shm_name_ = shm_prefix_ + std::to_string(channel_id);
  fixed_address_ = 0x100100000ULL + (channel_id % 100) * 0x200000ULL;
}

PosixSegment::PosixSegment(std::string prefix, uint64_t channel_id, size_t size)
    : Segment(channel_id, size) {
  shm_name_ = prefix + std::to_string(channel_id);
  fixed_address_ = 0x100100000ULL + (channel_id % 100) * 0x200000ULL;
}

PosixSegment::~PosixSegment() { PosixSegment::Destroy(); }

bool PosixSegment::OpenOrCreate(bool& open_only) {
  if (init_) {
    return true;
  }

  int fd = shm_open(shm_name_.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd < 0) {
    if (EEXIST == errno) {
      PANGOFLY_DEBUG << "shm already exist, open only: " << shm_name_ << std::endl;
      open_only = true;
      return OpenOnly();
    } else {
      std::error_code ec(errno, std::generic_category());
      PANGOFLY_ERROR << "create shm failed: " << shm_name_ << ", error: " << ec.message() << std::endl;
      return false;
    }
  }

  if (ftruncate(fd, static_cast<long>(conf_.managed_shm_size())) < 0) {
    std::error_code ec(errno, std::generic_category());
    PANGOFLY_ERROR << "ftruncate failed: " << ec.message() << std::endl;
    close(fd);
    return false;
  }

  void* desired_addr = reinterpret_cast<void*>(fixed_address_);
  PANGOFLY_INFO << "Trying to map shm at fixed address: " << desired_addr << ", size: " << conf_.managed_shm_size() << std::endl;
  
  managed_shm_ = mmap(desired_addr, conf_.managed_shm_size(), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_FIXED, fd, 0);
  
  if (managed_shm_ == MAP_FAILED || managed_shm_ != desired_addr) {
    PANGOFLY_WARN << "Fixed address mapping failed, trying any address..." << std::endl;
    
    managed_shm_ = mmap(nullptr, conf_.managed_shm_size(), PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    if (managed_shm_ == MAP_FAILED) {
      std::error_code ec(errno, std::generic_category());
      PANGOFLY_ERROR << "mmap failed: " << ec.message() << std::endl;
      close(fd);
      shm_unlink(shm_name_.c_str());
      return false;
    }
    PANGOFLY_WARN << "Using fallback address: " << managed_shm_ << std::endl;
  } else {
    PANGOFLY_INFO << "Successfully mapped shm to fixed address: " << managed_shm_ << std::endl;
  }
  
  // 调试：检查共享内存内容
  PANGOFLY_INFO << "shm_name: " << shm_name_ << ", fd: " << fd << std::endl;

  close(fd);

  state_ = new (managed_shm_) State(conf_.ceiling_msg_size());
  if (state_ == nullptr) {
    PANGOFLY_ERROR << "create state failed." << std::endl;
    munmap(managed_shm_, conf_.managed_shm_size());
    managed_shm_ = nullptr;
    shm_unlink(shm_name_.c_str());
    return false;
  }

  conf_.Update(state_->ceiling_msg_size());

  blocks_ = reinterpret_cast<Block*>(static_cast<char*>(managed_shm_) + sizeof(State));
  if (blocks_ == nullptr) {
    PANGOFLY_ERROR << "create blocks failed." << std::endl;
    state_->~State();
    state_ = nullptr;
    munmap(managed_shm_, conf_.managed_shm_size());
    managed_shm_ = nullptr;
    shm_unlink(shm_name_.c_str());
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

bool PosixSegment::OpenOnly() {
  if (init_) {
    return true;
  }

  int fd = shm_open(shm_name_.c_str(), O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd == -1) {
    std::error_code ec(errno, std::generic_category());
    PANGOFLY_ERROR << "get shm failed: " << ec.message() << std::endl;
    return false;
  }

  struct stat file_attr;
  if (fstat(fd, &file_attr) < 0) {
    std::error_code ec(errno, std::generic_category());
    PANGOFLY_ERROR << "fstat failed: " << ec.message() << std::endl;
    close(fd);
    return false;
  }

  void* desired_addr = reinterpret_cast<void*>(fixed_address_);
  PANGOFLY_INFO << "Trying to open shm at fixed address: " << desired_addr << ", size: " << file_attr.st_size << std::endl;
  
  managed_shm_ = mmap(desired_addr, static_cast<size_t>(file_attr.st_size),
                      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  
  if (managed_shm_ == MAP_FAILED || managed_shm_ != desired_addr) {
    PANGOFLY_WARN << "Fixed address mapping failed, trying any address..." << std::endl;
    
    managed_shm_ = mmap(nullptr, static_cast<size_t>(file_attr.st_size),
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (managed_shm_ == MAP_FAILED) {
      std::error_code ec(errno, std::generic_category());
      PANGOFLY_ERROR << "mmap failed: " << ec.message() << std::endl;
      close(fd);
      return false;
    }
    PANGOFLY_WARN << "Using fallback address: " << managed_shm_ << std::endl;
  } else {
    PANGOFLY_INFO << "Successfully mapped existing shm to fixed address: " << managed_shm_ << std::endl;
  }

  close(fd);

  state_ = reinterpret_cast<State*>(managed_shm_);
  if (state_ == nullptr) {
    PANGOFLY_ERROR << "get state failed." << std::endl;
    munmap(managed_shm_, static_cast<size_t>(file_attr.st_size));
    managed_shm_ = nullptr;
    return false;
  }

  conf_.Update(state_->ceiling_msg_size());

  blocks_ = reinterpret_cast<Block*>(static_cast<char*>(managed_shm_) + sizeof(State));
  if (blocks_ == nullptr) {
    PANGOFLY_ERROR << "get blocks failed." << std::endl;
    state_ = nullptr;
    munmap(managed_shm_, conf_.managed_shm_size());
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

bool PosixSegment::Remove() {
  if (shm_unlink(shm_name_.c_str()) < 0) {
    std::error_code ec(errno, std::generic_category());
    PANGOFLY_ERROR << "shm_unlink failed: " << ec.message() << std::endl;
    return false;
  }
  return true;
}

void PosixSegment::Reset() {
  state_ = nullptr;
  blocks_ = nullptr;
  block_buf_addrs_.clear();
  if (managed_shm_ != nullptr) {
    munmap(managed_shm_, conf_.managed_shm_size());
    managed_shm_ = nullptr;
  }
}

} // namespace transport
} // namespace pangofly