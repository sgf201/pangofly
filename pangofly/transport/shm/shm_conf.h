#ifndef PANGOFLY_TRANSPORT_SHM_SHM_CONF_H_
#define PANGOFLY_TRANSPORT_SHM_SHM_CONF_H_

#include <cstdint>
#include <string>

namespace pangofly {
namespace transport {

class ShmConf {
public:
  explicit ShmConf(std::string channel_name);
  ShmConf(const uint32_t& real_msg_size, uint64_t channel_id, bool single_mode = false);
  virtual ~ShmConf() = default;

  void Update(const uint32_t& real_msg_size);

  const uint32_t& ceiling_msg_size() const { return ceiling_msg_size_; }
  const uint32_t& block_buf_size() const { return block_buf_size_; }
  const uint16_t& block_num() const { return block_num_; }
  const uint64_t& managed_shm_size() const { return managed_shm_size_; }

private:
  uint32_t GetCeilingMessageSize(const uint32_t& real_msg_size);
  uint32_t GetBlockBufSize(const uint32_t& ceiling_message_size);
  uint16_t GetBlockNum(const uint32_t& ceiling_message_size);

  uint32_t ceiling_msg_size_ = 0;
  uint32_t block_buf_size_ = 0;
  uint16_t block_num_ = 0;
  uint64_t managed_shm_size_ = 0;
  bool single_mode_ = false;
  uint64_t channel_id_ = 0;

  static const uint32_t EXTRA_SIZE;
  static const uint32_t SHM_BLOCK_SIZE;
  static const uint32_t MESSAGE_INFO_SIZE;
  static const uint32_t MESSAGE_SIZE_1K;
};

} // namespace transport
} // namespace pangofly

#endif // PANGOFLY_TRANSPORT_SHM_SHM_CONF_H_