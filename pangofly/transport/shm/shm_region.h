#ifndef PANGOFLY_TRANSPORT_SHM_SHM_REGION_H_
#define PANGOFLY_TRANSPORT_SHM_SHM_REGION_H_

#include <cstdint>
#include <string>

namespace pangofly {
namespace transport {

class ShmRegion {
public:
  static uint64_t GetReserveAddr();
  static uint64_t GetReserveSize();
  static uint64_t GetReserveEnd();

private:
  static void LoadConfig();
  static bool config_loaded_;
  static uint64_t reserve_addr_;
  static uint64_t reserve_size_;

  static uint64_t ReadFromSysfs(const std::string& path, uint64_t default_val);
  static uint64_t ReadFromEnv(const std::string& env_name, uint64_t default_val);
};

} // namespace transport
} // namespace pangofly

#endif // PANGOFLY_TRANSPORT_SHM_SHM_REGION_H_
