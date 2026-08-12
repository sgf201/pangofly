#ifndef PANGOFLY_MEMORY_SHM_ALLOCATOR_H_
#define PANGOFLY_MEMORY_SHM_ALLOCATOR_H_

#include <cstddef>
#include <type_traits>
#include <vector>
#include <string>

#include "core/memory/pmr.h"
#include "core/memory/allocator.h"

namespace pangofly {

// Stateful allocator that captures the CurrentAllocator's memory_resource at
// construction time.  Containers using ShmAllocator (e.g. std::vector,
// std::basic_string) will automatically allocate from whatever resource is
// currently bound via ScopedAllocator — shared memory inside LoanSample scope,
// or the global heap otherwise — without the user having to pass a resource
// explicitly.
template <typename T>
class ShmAllocator {
public:
    using value_type        = T;
    using pointer           = T*;
    using const_pointer     = const T*;
    using reference         = T&;
    using const_reference   = const T&;
    using size_type         = std::size_t;
    using difference_type   = std::ptrdiff_t;

    template <typename U>
    struct rebind { using other = ShmAllocator<U>; };

    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::false_type;

    // Default ctor: grab the thread-local CurrentAllocator resource.
    ShmAllocator() noexcept
        : resource_(pangofly::CurrentAllocator::resource()) {}

    // Explicit resource (e.g. for testing or non-default scopes).
    explicit ShmAllocator(pangofly::pmr::memory_resource* r) noexcept
        : resource_(r ? r : pangofly::CurrentAllocator::resource()) {}

    // Rebind copy ctor.
    template <typename U>
    ShmAllocator(const ShmAllocator<U>& other) noexcept
        : resource_(other.resource()) {}

    T* allocate(size_type n) {
        if (n == 0) return nullptr;
        return static_cast<T*>(
            resource_->allocate(n * sizeof(T), alignof(T)));
    }

    void deallocate(T* p, size_type n) noexcept {
        if (p) {
            resource_->deallocate(p, n * sizeof(T), alignof(T));
        }
    }

    pangofly::pmr::memory_resource* resource() const noexcept { return resource_; }

private:
    pangofly::pmr::memory_resource* resource_;
};

template <typename T, typename U>
inline bool operator==(const ShmAllocator<T>& a, const ShmAllocator<U>& b) noexcept {
    return a.resource() == b.resource();
}

template <typename T, typename U>
inline bool operator!=(const ShmAllocator<T>& a, const ShmAllocator<U>& b) noexcept {
    return !(a == b);
}

// Convenient aliases — these are what IDL codegen emits.
template <typename T>
using shm_vector = std::vector<T, ShmAllocator<T>>;

using shm_string = std::basic_string<char, std::char_traits<char>,
                                      ShmAllocator<char>>;

} // namespace pangofly

#endif // PANGOFLY_MEMORY_SHM_ALLOCATOR_H_
