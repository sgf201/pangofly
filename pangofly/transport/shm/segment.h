#ifndef PANGOFLY_TRANSPORT_SHM_SEGMENT_H_
#define PANGOFLY_TRANSPORT_SHM_SEGMENT_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "pangofly/transport/shm/block.h"
#include "pangofly/transport/shm/shm_conf.h"
#include "pangofly/transport/shm/state.h"

namespace pangofly {
namespace transport {

class Segment;
using SegmentPtr = std::shared_ptr<Segment>;

struct WritableBlock {
  uint16_t index = 0;
  uint16_t msg_type = 0;
  uint32_t ceiling_msg_size = 0;
  Block* block = nullptr;
  uint8_t* buf = nullptr;
};
using ReadableBlock = WritableBlock;

class Segment {
public:
  explicit Segment(uint64_t channel_id, std::string channel_name);
  explicit Segment(uint64_t channel_id, std::size_t min_size);
  virtual ~Segment() = default;

  bool AcquireBlockToWrite(std::size_t msg_size, WritableBlock* writable_block);
  void ReleaseWrittenBlock(const WritableBlock& writable_block);

  bool AcquireBlockToRead(ReadableBlock* readable_block);
  bool AcquireFixedBlock(std::size_t msg_size, WritableBlock* writable_block, bool& open_only);
  void ReleaseReadBlock(const ReadableBlock& readable_block);

  bool HasOwnerShip(char* ptr);
  std::recursive_mutex& GetSegmentLock();

  template <typename T, class... Args>
  T* GetOrCreateObj(Args&&... args) {
    WritableBlock wb;
    bool open_only = false;
    if (!AcquireFixedBlock(0, &wb, open_only)) {
      return nullptr;
    }

    T* obj_ptr = nullptr;
    if (open_only) {
      obj_ptr = reinterpret_cast<T*>(wb.buf);
    } else {
      obj_ptr = new (wb.buf) T(std::forward<Args>(args)...);
    }
    ReleaseWrittenBlock(wb);
    return obj_ptr;
  }

  virtual bool OpenOrCreate(bool& open_only) = 0;
  virtual bool OpenOrCreate();

protected:
  virtual bool Destroy();
  virtual void Reset() = 0;
  virtual bool Remove() = 0;
  virtual bool OpenOnly() = 0;

  bool init_ = false;
  std::recursive_mutex segment_lock_;
  ShmConf conf_;
  uint64_t channel_id_;
  std::string channel_name_;

  State* state_ = nullptr;
  Block* blocks_ = nullptr;
  void* managed_shm_ = nullptr;
  std::unordered_map<uint32_t, uint8_t*> block_buf_addrs_;
  static std::string shm_prefix_;

private:
  bool Remap();
  bool Recreate(const uint64_t& msg_size);
  uint16_t GetNextWritableBlockIndex();
};

} // namespace transport
} // namespace pangofly

#endif // PANGOFLY_TRANSPORT_SHM_SEGMENT_H_