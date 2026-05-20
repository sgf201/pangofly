#pragma once

#include "block_allocator.h"
#include <cstddef>
#include <cstdint>
#include <atomic>

namespace pangofly {

constexpr size_t DEFAULT_BLOCK_SIZE = 64 * 1024;
constexpr size_t DEFAULT_NUM_BLOCKS = 16;

class ChannelBlockPoolManager {
public:
    ChannelBlockPoolManager() 
        : pool_(nullptr), 
          pool_memory_(nullptr),
          total_blocks_(0),
          block_size_(0) {}
    
    ~ChannelBlockPoolManager() {
        destroy();
    }
    
    bool create(size_t block_size = DEFAULT_BLOCK_SIZE, 
                size_t num_blocks = DEFAULT_NUM_BLOCKS) {
        destroy();
        
        block_size_ = block_size;
        total_blocks_ = num_blocks;
        
        size_t pool_header_size = sizeof(ChannelBlockPool);
        size_t blocks_size = num_blocks * block_size;
        size_t total_size = pool_header_size + blocks_size;
        
        pool_memory_ = new uint8_t[total_size];
        if (!pool_memory_) {
            return false;
        }
        
        pool_ = reinterpret_cast<ChannelBlockPool*>(pool_memory_);
        pool_->magic = PANGOFLY_BLOCK_MAGIC;
        pool_->num_blocks = static_cast<uint32_t>(num_blocks);
        pool_->block_size = static_cast<uint32_t>(block_size);
        pool_->free_count.store(static_cast<uint32_t>(num_blocks));
        
        uint8_t* block_memory = pool_memory_ + pool_header_size;
        
        pool_->free_list = nullptr;
        for (size_t i = 0; i < num_blocks; ++i) {
            BlockHeader* block = reinterpret_cast<BlockHeader*>(
                block_memory + i * block_size
            );
            
            block->magic = PANGOFLY_BLOCK_MAGIC;
            block->block_size = static_cast<uint32_t>(block_size);
            block->used_size = 0;
            block->status.store(BLOCK_FREE);
            
            block->next = pool_->free_list;
            block->prev = nullptr;
            
            if (pool_->free_list) {
                pool_->free_list->prev = block;
            }
            
            pool_->free_list = block;
        }
        
        return true;
    }
    
    bool attach(void* memory, size_t size) {
        destroy();
        
        pool_ = reinterpret_cast<ChannelBlockPool*>(memory);
        pool_memory_ = reinterpret_cast<uint8_t*>(memory);
        
        if (pool_->magic != PANGOFLY_BLOCK_MAGIC) {
            pool_ = nullptr;
            pool_memory_ = nullptr;
            return false;
        }
        
        block_size_ = pool_->block_size;
        total_blocks_ = pool_->num_blocks;
        
        return true;
    }
    
    void destroy() {
        if (pool_memory_) {
            delete[] pool_memory_;
            pool_memory_ = nullptr;
        }
        pool_ = nullptr;
        total_blocks_ = 0;
        block_size_ = 0;
    }
    
    BlockAllocator create_block_allocator() {
        BlockAllocator allocator;
        if (pool_) {
            allocator.initialize(pool_);
        }
        return allocator;
    }
    
    bool is_valid() const {
        return pool_ && pool_->magic == PANGOFLY_BLOCK_MAGIC;
    }
    
    size_t get_block_size() const { return block_size_; }
    size_t get_total_blocks() const { return total_blocks_; }
    size_t get_free_blocks() const { 
        return pool_ ? pool_->free_count.load() : 0; 
    }
    
    void* get_pool_memory() const { return pool_memory_; }
    size_t get_pool_size() const {
        return sizeof(ChannelBlockPool) + total_blocks_ * block_size_;
    }
    
private:
    ChannelBlockPool* pool_;
    uint8_t* pool_memory_;
    size_t total_blocks_;
    size_t block_size_;
};

} // namespace pangofly
