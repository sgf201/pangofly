#ifndef PANGOFLY_MEMORY_SHM_RESOURCE_H_
#define PANGOFLY_MEMORY_SHM_RESOURCE_H_

#include <cstddef>

#include "core/common/macros.h"
#include "core/memory/pmr.h"
#include "idl/allocator/block_allocator.h"

namespace pangofly {

// ============================================================================
// ShmResource
//
// Adapts pangofly's BlockAllocator to std::pmr::memory_resource interface.
// This allows PMR containers (std::pmr::vector, std::pmr::string, etc.) to
// allocate memory from the shared memory block managed by BlockAllocator.
//
// Usage:
//   BlockAllocator block_alloc;
//   block_alloc.initialize_from_buffer(shm_ptr, shm_size);
//   ShmResource resource(&block_alloc);
//
//   // Now PMR containers can use the shared memory:
//   std::pmr::vector<int> vec(&resource);
//   vec.resize(100);  // allocates from shared memory
//
// ============================================================================
class ShmResource : public pangofly::pmr::memory_resource {
public:
    explicit ShmResource(BlockAllocator* alloc) : alloc_(alloc) {}

    PANGOFLY_DISALLOW_COPY_AND_ASSIGN(ShmResource);

    BlockAllocator* allocator() const { return alloc_; }

protected:
    void* do_allocate(size_t bytes, size_t alignment) override {
        (void)alignment;
        if (!alloc_) return nullptr;
        return alloc_->allocate(bytes);
    }

    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        (void)bytes;
        (void)alignment;
        // Monotonic pool: deallocation is a no-op.
        // Memory is reclaimed when the block is returned to the pool.
    }

    bool do_is_equal(const pangofly::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

private:
    BlockAllocator* alloc_;
};

} // namespace pangofly

#endif // PANGOFLY_MEMORY_SHM_RESOURCE_H_
