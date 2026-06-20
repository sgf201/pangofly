#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cstring>

namespace pangofly {

constexpr uint64_t PANGOFLY_BLOCK_MAGIC = 0x50424C4B56313000ULL; // "PBLKV10"

enum BlockStatus {
    BLOCK_FREE = 0,
    BLOCK_RESERVED = 1,
    BLOCK_IN_USE = 2
};

struct BlockHeader {
    uint64_t magic;
    uint32_t block_size;
    uint32_t used_size;
    std::atomic<uint32_t> status;
    BlockHeader* next;
    BlockHeader* prev;
};

struct ChannelBlockPool {
    uint64_t magic;
    uint32_t num_blocks;
    uint32_t block_size;
    std::atomic<BlockHeader*> free_list;
    std::atomic<uint32_t> free_count;
    uint8_t padding[40];
};

class BlockAllocator {
public:
    BlockAllocator() : pool_(nullptr), block_size_(0), buffer_(nullptr), buffer_size_(0) {}

    void initialize(ChannelBlockPool* pool) {
        pool_ = pool;
        block_size_ = pool->block_size;
    }

    void initialize_from_buffer(void* buffer, size_t size) {
        buffer_ = reinterpret_cast<uint8_t*>(buffer);
        buffer_size_ = size;
        block_size_ = size;
        pool_ = nullptr;
    }

    void* allocate(size_t size) {
        if (size == 0) return nullptr;

        if (buffer_) {
            if (size > buffer_size_) return nullptr;
            uint8_t* ptr = buffer_;
            buffer_ += size;
            buffer_size_ -= size;
            return ptr;
        }

        if (!pool_ || size == 0) return nullptr;

        if (size > get_max_allocation_size()) {
            return nullptr;
        }
        
        BlockHeader* block = acquire_block();
        if (!block) return nullptr;
        
        size_t offset = sizeof(BlockHeader);
        void* ptr = reinterpret_cast<char*>(block) + offset;
        
        block->used_size = static_cast<uint32_t>(size);
        return ptr;
    }
    
    void deallocate(void* ptr) {
        if (!ptr || !pool_) return;
        
        BlockHeader* block = reinterpret_cast<BlockHeader*>(
            reinterpret_cast<char*>(ptr) - sizeof(BlockHeader)
        );
        
        if (block->magic != PANGOFLY_BLOCK_MAGIC) {
            return;
        }
        
        release_block(block);
    }
    
    void deallocate_block(void* ptr) {
        if (!ptr || !pool_) return;
        
        BlockHeader* block = reinterpret_cast<BlockHeader*>(
            reinterpret_cast<char*>(ptr) - sizeof(BlockHeader)
        );
        
        if (block->magic != PANGOFLY_BLOCK_MAGIC) {
            return;
        }
        
        release_block(block);
    }
    
    size_t get_block_size() const { return block_size_; }
    size_t get_max_allocation_size() const {
        return block_size_ - sizeof(BlockHeader);
    }
    
private:
    BlockHeader* acquire_block() {
        BlockHeader* block = nullptr;
        
        while (true) {
            block = pool_->free_list;
            if (!block) return nullptr;
            
            uint32_t expected = BLOCK_FREE;
            if (block->status.compare_exchange_strong(expected, BLOCK_RESERVED)) {
                break;
            }
            
            block = nullptr;
        }
        
        if (block) {
            remove_from_free_list(block);
            block->status.store(BLOCK_IN_USE);
            pool_->free_count.fetch_sub(1);
        }
        
        return block;
    }
    
    void release_block(BlockHeader* block) {
        block->status.store(BLOCK_FREE);
        add_to_free_list(block);
        pool_->free_count.fetch_add(1);
    }
    
    void add_to_free_list(BlockHeader* block) {
        while (true) {
            BlockHeader* head = pool_->free_list;
            block->next = head;
            block->prev = nullptr;
            if (head) {
                head->prev = block;
            }
            if (pool_->free_list.compare_exchange_strong(head, block)) {
                break;
            }
        }
    }
    
    void remove_from_free_list(BlockHeader* block) {
        while (true) {
            BlockHeader* prev = block->prev;
            BlockHeader* next = block->next;
            
            if (prev) {
                prev->next = next;
            } else {
                pool_->free_list.compare_exchange_strong(block, next);
            }
            
            if (next) {
                next->prev = prev;
            }
            
            break;
        }
    }
    
    ChannelBlockPool* pool_;
    size_t block_size_;
    uint8_t* buffer_;
    size_t buffer_size_;
};

template<typename T>
class PangoflyAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    
    template<typename U>
    struct rebind {
        using other = PangoflyAllocator<U>;
    };
    
    PangoflyAllocator() : block_allocator_(nullptr) {}
    
    explicit PangoflyAllocator(BlockAllocator* block_allocator)
        : block_allocator_(block_allocator) {}
    
    template<typename U>
    PangoflyAllocator(const PangoflyAllocator<U>& other)
        : block_allocator_(other.block_allocator_) {}
    
    pointer allocate(size_type n) {
        if (!block_allocator_) {
            return nullptr;
        }
        
        size_type size = n * sizeof(T);
        void* ptr = block_allocator_->allocate(size);
        return static_cast<pointer>(ptr);
    }
    
    void deallocate(pointer p, size_type /*n*/) {
        if (p && block_allocator_) {
            block_allocator_->deallocate(p);
        }
    }
    
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }
    
    template<typename U>
    void destroy(U* p) {
        p->~U();
    }
    
    BlockAllocator* get_block_allocator() const {
        return block_allocator_;
    }
    
private:
    BlockAllocator* block_allocator_;
    
    template<typename U>
    friend class PangoflyAllocator;
};

template<typename T, typename U>
bool operator==(const PangoflyAllocator<T>& a, const PangoflyAllocator<U>& b) {
    return a.get_block_allocator() == b.get_block_allocator();
}

template<typename T, typename U>
bool operator!=(const PangoflyAllocator<T>& a, const PangoflyAllocator<U>& b) {
    return !(a == b);
}

} // namespace pangofly
