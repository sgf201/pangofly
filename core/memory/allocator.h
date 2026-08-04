#ifndef PANGOFLY_MEMORY_ALLOCATOR_H_
#define PANGOFLY_MEMORY_ALLOCATOR_H_

#include <mutex>

#include "core/common/macros.h"
#include "core/memory/pmr.h"

namespace pangofly {

// ============================================================================
// CurrentAllocator
//
// Manages a thread_local memory_resource pointer. Each thread has its own
// "current allocator" which can be bound/unbound at runtime.
//
// Usage:
//   CurrentAllocator::begin(&my_pool);
//   // ... all objects constructed here use my_pool ...
//   CurrentAllocator::end();
//
// ============================================================================
class CurrentAllocator {
public:
    // Bind a memory_resource as the current allocator (call at scope entry)
    static void begin(pangofly::pmr::memory_resource* r) {
        current_resource() = r;
    }

    // Restore to the global default resource
    static void end() {
        current_resource() = pangofly::pmr::get_default_resource();
    }

    // Get the currently bound memory_resource
    static pangofly::pmr::memory_resource* resource() {
        return current_resource();
    }

    // Check if a non-default allocator is active
    static bool is_custom() {
        return current_resource() != pangofly::pmr::get_default_resource();
    }

private:
    static pangofly::pmr::memory_resource*& current_resource() {
        thread_local pangofly::pmr::memory_resource* r =
            pangofly::pmr::get_default_resource();
        return r;
    }
};

// ============================================================================
// ScopedAllocator
//
// RAII guard that saves the current allocator on construction and restores
// it on destruction. Ensures exception-safe allocator state rollback.
//
// Usage:
//   {
//       ScopedAllocator scope(&my_pool);
//       MyObject obj;       // uses my_pool
//       pmr::vector<int> v; // uses my_pool (via CurrentAllocator::resource())
//   } // auto-restore previous allocator
//
// ============================================================================
class ScopedAllocator {
public:
    PANGOFLY_DISALLOW_COPY_AND_ASSIGN(ScopedAllocator);

    explicit ScopedAllocator(pangofly::pmr::memory_resource* r) {
        prev_ = CurrentAllocator::resource();
        CurrentAllocator::begin(r);
    }

    ~ScopedAllocator() {
        CurrentAllocator::begin(prev_);
    }

private:
    pangofly::pmr::memory_resource* prev_;
};

} // namespace pangofly

#endif // PANGOFLY_MEMORY_ALLOCATOR_H_
