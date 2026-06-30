#ifndef PANGOFLY_TRANSPORT_SHM_BLOCK_H_
#define PANGOFLY_TRANSPORT_SHM_BLOCK_H_

#include <cstdint>
#include <atomic>

namespace pangofly {
namespace transport {

class Block {
public:
  Block() = default;
  ~Block() = default;

  uint32_t msg_size() const { return msg_size_; }
  void set_msg_size(uint32_t size) { msg_size_ = size; }

  uint32_t sequence() const { return sequence_.load(std::memory_order_acquire); }
  void set_sequence(uint32_t seq) { 
    sequence_.store(seq, std::memory_order_release);
  }

private:
  uint32_t msg_size_ = 0;
  std::atomic<uint32_t> sequence_{0};
};

} // namespace transport
} // namespace pangofly

#endif // PANGOFLY_TRANSPORT_SHM_BLOCK_H_