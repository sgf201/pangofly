#ifndef PANGOFLY_MESSAGE_MESSAGE_INFO_H_
#define PANGOFLY_MESSAGE_MESSAGE_INFO_H_

#include <cstdint>

namespace pangofly {

struct MessageInfo {
  uint64_t seq = 0;
  uint64_t timestamp = 0;
  uint32_t src_node_id = 0;
  uint32_t dst_node_id = 0;
};

} // namespace pangofly

#endif // PANGOFLY_MESSAGE_MESSAGE_INFO_H_