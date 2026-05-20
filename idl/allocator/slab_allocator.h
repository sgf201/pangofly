#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>
#include <memory>

namespace pangofly {

constexpr uint32_t PANGOFLY_MAGIC = 0x50414E474F464C59ULL; // "PANGOFLEY"

constexpr size_t DEFAULT_CHUNK_SIZE = 16 * 1024 * 1024; // 16MB
constexpr size_t SLAB_SIZE_MIN = 16;
constexpr size_t SLAB_SIZE_MAX = 4096;
constexpr size_t NUM_SLAB_SIZES = 8;

struct SlabBlock {
    bool in_use;
    SlabBlock* next;
};

struct Slab {
    size_t block_size;
    size_t num_blocks;
    void* memory;
    SlabBlock* free_list;
    size_t used_count;
};

class SlabAllocator {
public:
    explicit SlabAllocator(void* base, size_t size);
    ~SlabAllocator();
    
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
    
    size_t get_used_size() const { return used_size_; }
    size_t get_total_size() const { return total_size_; }
    
    void reset();
    
private:
    size_t get_slab_index(size_t size) const;
    Slab& get_slab(size_t index);
    
    void* base_;
    size_t total_size_;
    size_t used_size_;
    
    std::vector<Slab> slabs_;
    std::mutex mutex_;
    
    static const size_t SLAB_SIZES[NUM_SLAB_SIZES];
};

const size_t SlabAllocator::SLAB_SIZES[NUM_SLAB_SIZES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

class ChunkHeader {
public:
    uint64_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t used_size;
    uint32_t num_vectors;
    uint32_t num_strings;
    uint32_t flags;
    
    static constexpr uint32_t CURRENT_VERSION = 1;
    
    ChunkHeader(size_t size)
        : magic(PANGOFLY_MAGIC),
          version(CURRENT_VERSION),
          total_size(static_cast<uint32_t>(size)),
          used_size(sizeof(ChunkHeader)),
          num_vectors(0),
          num_strings(0),
          flags(0) {}
    
    bool is_valid() const {
        return magic == PANGOFLY_MAGIC;
    }
    
    void* get_data_area() {
        return reinterpret_cast<char*>(this) + sizeof(ChunkHeader);
    }
    
    size_t get_data_area_size() const {
        return total_size - sizeof(ChunkHeader);
    }
};

class PangoflyChunk {
public:
    PangoflyChunk() = default;
    explicit PangoflyChunk(size_t size);
    
    bool initialize(size_t size);
    void reset();
    
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
    
    template<typename T>
    T* allocate() {
        return static_cast<T*>(allocate(sizeof(T)));
    }
    
    template<typename T>
    void deallocate(T* ptr) {
        deallocate(ptr, sizeof(T));
    }
    
    bool is_valid() const { return header_ && header_->is_valid(); }
    size_t get_total_size() const { return total_size_; }
    size_t get_used_size() const { return header_ ? header_->used_size : 0; }
    size_t get_available_size() const { return total_size_ - get_used_size(); }
    
private:
    ChunkHeader* header_;
    SlabAllocator* allocator_;
    void* memory_;
    size_t total_size_;
};

} // namespace pangofly
