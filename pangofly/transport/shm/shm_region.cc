#include "pangofly/transport/shm/shm_region.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace pangofly {
namespace transport {

bool ShmRegion::config_loaded_ = false;
uint64_t ShmRegion::reserve_addr_ = 0;
uint64_t ShmRegion::reserve_size_ = 0;

uint64_t ShmRegion::ReadFromSysfs(const std::string& path, uint64_t default_val) {
  std::ifstream file(path);
  if (file.is_open()) {
    std::string line;
    if (std::getline(file, line)) {
      try {
        return std::stoull(line, nullptr, 0);
      } catch (...) {
        return default_val;
      }
    }
    file.close();
  }
  return default_val;
}

uint64_t ShmRegion::ReadFromEnv(const std::string& env_name, uint64_t default_val) {
  const char* env_val = std::getenv(env_name.c_str());
  if (env_val != nullptr && strlen(env_val) > 0) {
    try {
      return std::stoull(env_val, nullptr, 0);
    } catch (...) {
      return default_val;
    }
  }
  return default_val;
}

void ShmRegion::LoadConfig() {
  if (config_loaded_) {
    return;
  }

#ifdef PANGOFLY_RESERVE_ADDR
  uint64_t default_addr = PANGOFLY_RESERVE_ADDR;
#else
  uint64_t default_addr = 0x60000000ULL;
#endif

#ifdef PANGOFLY_RESERVE_SIZE
  uint64_t default_size = PANGOFLY_RESERVE_SIZE;
#else
  uint64_t default_size = 0x10000000ULL;
#endif

  uint64_t addr = ReadFromEnv("PANGOFLY_RESERVE_ADDR", 0);
  if (addr == 0) {
    addr = ReadFromSysfs("/sys/module/pangofly/parameters/reserve_addr", 0);
  }
  if (addr == 0) {
    addr = default_addr;
  }

  uint64_t size = ReadFromEnv("PANGOFLY_RESERVE_SIZE", 0);
  if (size == 0) {
    size = ReadFromSysfs("/sys/module/pangofly/parameters/reserve_size", 0);
  }
  if (size == 0) {
    size = default_size;
  }

  reserve_addr_ = addr;
  reserve_size_ = size;
  config_loaded_ = true;
}

uint64_t ShmRegion::GetReserveAddr() {
  LoadConfig();
  return reserve_addr_;
}

uint64_t ShmRegion::GetReserveSize() {
  LoadConfig();
  return reserve_size_;
}

uint64_t ShmRegion::GetReserveEnd() {
  LoadConfig();
  return reserve_addr_ + reserve_size_;
}

} // namespace transport
} // namespace pangofly
