#ifndef PANGOFLY_TRANSPORT_SHM_K230_SHM_H_
#define PANGOFLY_TRANSPORT_SHM_K230_SHM_H_

#include <string>

#include "pangofly/transport/shm/segment.h"

namespace pangofly {
namespace transport {

class K230Shm : public Segment {
public:
  using Segment::OpenOrCreate;
  explicit K230Shm(uint64_t channel_id, std::string channel_name);
  explicit K230Shm(std::string prefix, uint64_t channel_id, size_t size);
  virtual ~K230Shm();

  static const char* Type() { return "k230"; }

  bool OpenOrCreate(bool& open_only) override;

private:
  void Reset() override;
  bool Remove() override;
  bool OpenOnly() override;

  std::string shm_name_;
  int shm_id_ = -1;
};

} // namespace transport
} // namespace pangofly

#endif // PANGOFLY_TRANSPORT_SHM_K230_SHM_H_