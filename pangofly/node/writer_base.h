#ifndef PANGOFLY_NODE_WRITER_BASE_H_
#define PANGOFLY_NODE_WRITER_BASE_H_

#include <memory>
#include <string>
#include <cstring>

#include "pangofly/node/writer.h"

#include "pangofly/transport/shm/posix_segment.h"
#include "idl/container/vector.h"
#define PANGOFLY_SEGMENT_TYPE transport::PosixSegment

namespace pangofly {

class WriterBase {
public:
  virtual ~WriterBase() = default;
  virtual const std::string& ChannelName() const = 0;
};

template <typename MessageT>
class ShmWriter : public Writer<MessageT>, public WriterBase {
public:
  explicit ShmWriter(const std::string& channel_name)
      : channel_name_(channel_name),
        segment_(std::make_shared<PANGOFLY_SEGMENT_TYPE>(
            std::hash<std::string>()(channel_name), channel_name)) {
    bool open_only = false;
    segment_->OpenOrCreate(open_only);
  }

  ~ShmWriter() override = default;

  bool Write(const MessageT& message) override {
    MessageInfo info;
    return Write(message, info);
  }

  bool Write(const MessageT& message, const MessageInfo& message_info) override {
    size_t total_size = calculate_message_size(message);

    transport::WritableBlock block;
    if (!segment_->AcquireBlockToWrite(total_size, &block)) {
      return false;
    }

    uint8_t* shm_base = reinterpret_cast<uint8_t*>(block.buf);
    size_t offset = 0;

    offset = serialize_to_shm(message, shm_base, offset);

    if (block.block != nullptr) {
      block.block->set_msg_size(total_size);
    }

    segment_->ReleaseWrittenBlock(block);
    return true;
  }

  const std::string& ChannelName() const override {
    return channel_name_;
  }

private:
  template<typename T>
  struct RemoveCV {
    using type = typename std::remove_cv<T>::type;
  };

  size_t calculate_message_size(const MessageT& message) {
    size_t total_size = sizeof(MessageT);

    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(&message);
    size_t struct_size = sizeof(MessageT);

    for (size_t i = 0; i < struct_size; i += sizeof(uintptr_t)) {
      uintptr_t maybe_ptr = *reinterpret_cast<const uintptr_t*>(data_ptr + i);
      if (maybe_ptr != 0) {
        const Vector<uint8_t>* vec = reinterpret_cast<const Vector<uint8_t>*>(data_ptr + i);
        total_size += vec->size() * sizeof(uint8_t);
      }
    }

    return total_size;
  }

  template<typename T>
  size_t serialize_vector(const Vector<T>& vec, uint8_t* shm_base, size_t offset) {
    size_t data_size = vec.size() * sizeof(T);
    memcpy(shm_base + offset, vec.data(), data_size);
    offset += data_size;
    return offset;
  }

  template<typename T>
  size_t serialize_primitive(const T& value, uint8_t* shm_base, size_t offset) {
    memcpy(shm_base + offset, &value, sizeof(T));
    return offset + sizeof(T);
  }

  size_t serialize_to_shm(const MessageT& message, uint8_t* shm_base, size_t offset) {
    const uint8_t* msg_ptr = reinterpret_cast<const uint8_t*>(&message);
    size_t struct_size = sizeof(MessageT);

    memcpy(shm_base, msg_ptr, struct_size);
    offset = struct_size;

    for (size_t i = 0; i < struct_size; i += sizeof(uintptr_t)) {
      uintptr_t vec_ptr = *reinterpret_cast<const uintptr_t*>(msg_ptr + i);
      if (vec_ptr != 0) {
        const Vector<uint8_t>* vec = reinterpret_cast<const Vector<uint8_t>*>(msg_ptr + i);
        size_t data_size = vec->size();

        Vector<uint8_t>* shm_vec = reinterpret_cast<Vector<uint8_t>*>(shm_base + i);
        shm_vec->~Vector();
        new (shm_vec) Vector<uint8_t>();

        if (data_size > 0) {
          memcpy(shm_base + offset, vec->data(), data_size);
          uint8_t* data_ptr = shm_base + offset;
          new (shm_vec) Vector<uint8_t>(data_ptr, data_size);
          offset += data_size;
        } else {
          new (shm_vec) Vector<uint8_t>();
        }
      }
    }

    return offset;
  }

  std::string channel_name_;
  std::shared_ptr<PANGOFLY_SEGMENT_TYPE> segment_;
};

} // namespace pangofly

#endif // PANGOFLY_NODE_WRITER_BASE_H_