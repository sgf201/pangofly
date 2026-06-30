#ifndef PANGOFLY_TRANSPORT_SHM_STATE_H_
#define PANGOFLY_TRANSPORT_SHM_STATE_H_

#include <cstdint>
#include <atomic>

namespace pangofly {
namespace transport {

class State {
public:
  explicit State(uint32_t ceiling_msg_size);
  ~State() = default;

  uint32_t ceiling_msg_size() const { return ceiling_msg_size_; }
  uint32_t reference_count() const { return reference_count_.load(); }
  
  void IncreaseReferenceCounts() { reference_count_.fetch_add(1); }
  void DecreaseReferenceCounts() { reference_count_.fetch_sub(1); }
  bool HasNoReference() const { return reference_count_.load() == 0; }

  uint32_t GetSequence(uint16_t block_index) const;
  void SetSequence(uint16_t block_index, uint32_t sequence);

private:
  uint32_t ceiling_msg_size_;
  std::atomic<uint32_t> reference_count_;
  std::atomic<uint32_t> sequences_[64];
};

} // namespace transport
} // namespace pangofly

#endif // PANGOFLY_TRANSPORT_SHM_STATE_H_