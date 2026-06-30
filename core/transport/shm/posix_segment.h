#ifndef PANGOFLY_TRANSPORT_SHM_POSIX_SEGMENT_H_
#define PANGOFLY_TRANSPORT_SHM_POSIX_SEGMENT_H_

#include <string>

#include "core/transport/shm/segment.h"

namespace pangofly {
namespace transport {

class PosixSegment : public Segment {
public:
  using Segment::OpenOrCreate;
  explicit PosixSegment(uint64_t channel_id, std::string channel_name);
  explicit PosixSegment(std::string prefix, uint64_t channel_id, size_t size);
  virtual ~PosixSegment();

  static const char* Type() { return "posix"; }

  bool OpenOrCreate(bool& open_only) override;

private:
  void Reset() override;
  bool Remove() override;
  bool OpenOnly() override;

  std::string shm_name_;
  uint64_t fixed_address_ = 0x100100000ULL;
};

} // namespace transport
} // namespace pangofly

#endif // PANGOFLY_TRANSPORT_SHM_POSIX_SEGMENT_H_