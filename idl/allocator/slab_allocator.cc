#include "slab_allocator.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace pangofly {

SlabAllocator::SlabAllocator(void* base, size_t size)
    : base_(base), total_size_(size), used_size_(0) {
    char* current = static_cast<char*>(base);
    size_t remaining = size;
    
    for (size_t i = 0; i < NUM_SLAB_SIZES && remaining > 0; ++i) {
        size_t block_size = SLAB_SIZES[i];
        size_t block_with_header = block_size + sizeof(SlabBlock);
        size_t num_blocks = remaining / (block_with_header * 16);
        
        if (num_blocks == 0) num_blocks = 1;
        
        size_t needed = num_blocks * block_with_header;
        if (needed > remaining) {
            needed = remaining;
            num_blocks = needed / block_with_header;
        }
        
        if (num_blocks > 0) {
            Slab slab;
            slab.block_size = block_size;
            slab.num_blocks = num_blocks;
            slab.memory = current;
            slab.used_count = 0;
            
            slab.free_list = nullptr;
            char* mem = static_cast<char*>(current);
            for (size_t b = 0; b < num_blocks; ++b) {
                SlabBlock* block = reinterpret_cast<SlabBlock*>(mem + b * block_with_header);
                block->in_use = false;
                block->next = slab.free_list;
                slab.free_list = block;
            }
            
            slabs_.push_back(slab);
            current += needed;
            remaining -= needed;
        }
    }
}

SlabAllocator::~SlabAllocator() {}

void* SlabAllocator::allocate(size_t size) {
    if (size == 0) return nullptr;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t index = get_slab_index(size);
    
    if (index < slabs_.size()) {
        Slab& slab = slabs_[index];
        if (slab.free_list) {
            SlabBlock* block = slab.free_list;
            slab.free_list = block->next;
            block->in_use = true;
            slab.used_count++;
            used_size_ += slab.block_size;
            return reinterpret_cast<char*>(block) + sizeof(SlabBlock);
        }
    }
    
    for (size_t i = index + 1; i < slabs_.size(); ++i) {
        Slab& slab = slabs_[i];
        if (slab.free_list) {
            SlabBlock* block = slab.free_list;
            slab.free_list = block->next;
            block->in_use = true;
            slab.used_count++;
            used_size_ += slab.block_size;
            return reinterpret_cast<char*>(block) + sizeof(SlabBlock);
        }
    }
    
    for (size_t i = 0; i < index && i < slabs_.size(); ++i) {
        Slab& slab = slabs_[i];
        if (slab.free_list) {
            SlabBlock* block = slab.free_list;
            slab.free_list = block->next;
            block->in_use = true;
            slab.used_count++;
            used_size_ += slab.block_size;
            return reinterpret_cast<char*>(block) + sizeof(SlabBlock);
        }
    }
    
    return nullptr;
}

void SlabAllocator::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t index = get_slab_index(size);
    size_t block_with_header = size + sizeof(SlabBlock);
    
    for (size_t i = index; i < slabs_.size(); ++i) {
        Slab& slab = slabs_[i];
        char* slab_start = static_cast<char*>(slab.memory);
        char* slab_end = slab_start + slab.num_blocks * (slab.block_size + sizeof(SlabBlock));
        char* ptr_char = static_cast<char*>(ptr);
        
        if (ptr_char >= slab_start && ptr_char < slab_end) {
            SlabBlock* block = reinterpret_cast<SlabBlock*>(ptr_char - sizeof(SlabBlock));
            if (block->in_use) {
                block->in_use = false;
                block->next = slab.free_list;
                slab.free_list = block;
                slab.used_count--;
                used_size_ -= slab.block_size;
            }
            return;
        }
    }
}

void SlabAllocator::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& slab : slabs_) {
        slab.free_list = nullptr;
        slab.used_count = 0;
        
        char* mem = static_cast<char*>(slab.memory);
        for (size_t b = 0; b < slab.num_blocks; ++b) {
            SlabBlock* block = reinterpret_cast<SlabBlock*>(mem + b * (slab.block_size + sizeof(SlabBlock)));
            block->in_use = false;
            block->next = slab.free_list;
            slab.free_list = block;
        }
    }
    
    used_size_ = 0;
}

size_t SlabAllocator::get_slab_index(size_t size) const {
    for (size_t i = 0; i < NUM_SLAB_SIZES; ++i) {
        if (size <= SLAB_SIZES[i]) {
            return i;
        }
    }
    return NUM_SLAB_SIZES;
}

typename SlabAllocator::Slab& SlabAllocator::get_slab(size_t index) {
    return slabs_[index];
}

PangoflyChunk::PangoflyChunk(size_t size) 
    : header_(nullptr), allocator_(nullptr), memory_(nullptr), total_size_(size) {}

bool PangoflyChunk::initialize(size_t size) {
    if (size < sizeof(ChunkHeader) + sizeof(SlabAllocator)) {
        return false;
    }
    
    memory_ = malloc(size);
    if (!memory_) {
        return false;
    }
    
    std::memset(memory_, 0, size);
    
    header_ = static_cast<ChunkHeader*>(memory_);
    new (header_) ChunkHeader(size);
    
    void* allocator_base = static_cast<char*>(memory_) + sizeof(ChunkHeader);
    size_t allocator_size = size - sizeof(ChunkHeader);
    
    allocator_ = new (allocator_base) SlabAllocator(
        static_cast<char*>(allocator_base) + sizeof(SlabAllocator),
        allocator_size - sizeof(SlabAllocator)
    );
    
    total_size_ = size;
    return true;
}

void PangoflyChunk::reset() {
    if (allocator_) {
        allocator_->reset();
    }
    if (header_) {
        header_->used_size = sizeof(ChunkHeader);
        header_->num_vectors = 0;
        header_->num_strings = 0;
    }
}

void* PangoflyChunk::allocate(size_t size) {
    if (!allocator_ || !header_) {
        return nullptr;
    }
    
    void* ptr = allocator_->allocate(size);
    if (ptr) {
        header_->used_size += size;
    }
    return ptr;
}

void PangoflyChunk::deallocate(void* ptr, size_t size) {
    if (!allocator_) {
        return;
    }
    
    allocator_->deallocate(ptr, size);
    header_->used_size -= size;
}

} // namespace pangofly
