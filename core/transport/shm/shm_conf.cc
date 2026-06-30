#include "core/transport/shm/shm_conf.h"
#include "core/transport/shm/state.h"

namespace pangofly {
namespace transport {

const uint32_t ShmConf::EXTRA_SIZE = 128;
const uint32_t ShmConf::SHM_BLOCK_SIZE = 32;
const uint32_t ShmConf::MESSAGE_INFO_SIZE = 48;
const uint32_t ShmConf::MESSAGE_SIZE_1K = 1024;

static constexpr uint32_t STATE_SIZE = sizeof(State);

ShmConf::ShmConf(std::string channel_name) : channel_id_(std::hash<std::string>()(channel_name)) {
  ceiling_msg_size_ = MESSAGE_SIZE_1K;
  block_buf_size_ = GetBlockBufSize(ceiling_msg_size_);
  block_num_ = GetBlockNum(ceiling_msg_size_);
  managed_shm_size_ = STATE_SIZE + block_num_ * (SHM_BLOCK_SIZE + block_buf_size_);
}

ShmConf::ShmConf(const uint32_t& real_msg_size, uint64_t channel_id, bool single_mode)
    : channel_id_(channel_id), single_mode_(single_mode) {
  ceiling_msg_size_ = GetCeilingMessageSize(real_msg_size);
  block_buf_size_ = GetBlockBufSize(ceiling_msg_size_);
  block_num_ = GetBlockNum(ceiling_msg_size_);
  managed_shm_size_ = STATE_SIZE + block_num_ * (SHM_BLOCK_SIZE + block_buf_size_);
}

void ShmConf::Update(const uint32_t& real_msg_size) {
  ceiling_msg_size_ = GetCeilingMessageSize(real_msg_size);
  block_buf_size_ = GetBlockBufSize(ceiling_msg_size_);
  block_num_ = GetBlockNum(ceiling_msg_size_);
  managed_shm_size_ = STATE_SIZE + block_num_ * (SHM_BLOCK_SIZE + block_buf_size_);
}

uint32_t ShmConf::GetCeilingMessageSize(const uint32_t& real_msg_size) {
  if (real_msg_size <= 64) return 64;
  if (real_msg_size <= 128) return 128;
  if (real_msg_size <= 256) return 256;
  if (real_msg_size <= 512) return 512;
  if (real_msg_size <= 1024) return 1024;
  if (real_msg_size <= 2048) return 2048;
  if (real_msg_size <= 4096) return 4096;
  if (real_msg_size <= 8192) return 8192;
  if (real_msg_size <= 16384) return 16384;
  if (real_msg_size <= 32768) return 32768;
  if (real_msg_size <= 65536) return 65536;
  if (real_msg_size <= 131072) return 131072;
  if (real_msg_size <= 262144) return 262144;
  if (real_msg_size <= 524288) return 524288;
  if (real_msg_size <= 1048576) return 1048576;
  if (real_msg_size <= 2097152) return 2097152;
  if (real_msg_size <= 4194304) return 4194304;
  return 8388608;
}

uint32_t ShmConf::GetBlockBufSize(const uint32_t& ceiling_message_size) {
  return ceiling_message_size + MESSAGE_INFO_SIZE + EXTRA_SIZE;
}

uint16_t ShmConf::GetBlockNum(const uint32_t& ceiling_message_size) {
  if (ceiling_message_size <= 1024) return 64;
  if (ceiling_message_size <= 4096) return 32;
  if (ceiling_message_size <= 16384) return 16;
  if (ceiling_message_size <= 65536) return 8;
  if (ceiling_message_size <= 262144) return 4;
  return 2;
}

} // namespace transport
} // namespace pangofly