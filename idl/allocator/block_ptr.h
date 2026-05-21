#pragma once

#include <memory>
#include <type_traits>

#include "block_allocator.h"

namespace pangofly {

template<typename T>
class BlockPtr {
public:
    BlockPtr() : data_(nullptr), block_allocator_(nullptr) {}
    
    BlockPtr(T* data, BlockAllocator* block_allocator)
        : data_(data), block_allocator_(block_allocator) {}
    
    BlockPtr(const BlockPtr&) = delete;
    BlockPtr& operator=(const BlockPtr&) = delete;
    
    BlockPtr(BlockPtr&& other) noexcept
        : data_(other.data_), block_allocator_(other.block_allocator_) {
        other.data_ = nullptr;
        other.block_allocator_ = nullptr;
    }
    
    BlockPtr& operator=(BlockPtr&& other) noexcept {
        if (this != &other) {
            reset();
            data_ = other.data_;
            block_allocator_ = other.block_allocator_;
            other.data_ = nullptr;
            other.block_allocator_ = nullptr;
        }
        return *this;
    }
    
    ~BlockPtr() {
        reset();
    }
    
    void reset() {
        if (block_allocator_ && data_) {
            block_allocator_->deallocate(data_);
        }
        data_ = nullptr;
        block_allocator_ = nullptr;
    }
    
    T* get() const { return data_; }
    
    T& operator*() const { return *data_; }
    
    T* operator->() const { return data_; }
    
    explicit operator bool() const { return data_ != nullptr; }
    
    BlockAllocator* get_block_allocator() const { return block_allocator_; }
    
    template<typename... Args>
    static BlockPtr<T> make(BlockAllocator* block_allocator) {
        if (!block_allocator) {
            return BlockPtr<T>();
        }
        
        size_t size = sizeof(T);
        void* raw = block_allocator->allocate(size);
        if (!raw) {
            return BlockPtr<T>();
        }
        
        T* data = new (raw) T();
        return BlockPtr<T>(data, block_allocator);
    }
    
    template<typename... Args>
    static BlockPtr<T> make(BlockAllocator* block_allocator, Args&&... args) {
        if (!block_allocator) {
            return BlockPtr<T>();
        }
        
        size_t size = sizeof(T);
        void* raw = block_allocator->allocate(size);
        if (!raw) {
            return BlockPtr<T>();
        }
        
        T* data = new (raw) T(std::forward<Args>(args)...);
        return BlockPtr<T>(data, block_allocator);
    }
    
private:
    T* data_;
    BlockAllocator* block_allocator_;
};

template<typename T>
class MessageBlockPtr {
public:
    MessageBlockPtr() : data_(nullptr), block_allocator_(nullptr), owns_block_(false) {}
    
    MessageBlockPtr(T* data, BlockAllocator* block_allocator, bool owns_block = true)
        : data_(data), block_allocator_(block_allocator), owns_block_(owns_block) {}
    
    MessageBlockPtr(const MessageBlockPtr&) = delete;
    MessageBlockPtr& operator=(const MessageBlockPtr&) = delete;
    
    MessageBlockPtr(MessageBlockPtr&& other) noexcept
        : data_(other.data_), 
          block_allocator_(other.block_allocator_),
          owns_block_(other.owns_block_) {
        other.data_ = nullptr;
        other.block_allocator_ = nullptr;
        other.owns_block_ = false;
    }
    
    MessageBlockPtr& operator=(MessageBlockPtr&& other) noexcept {
        if (this != &other) {
            reset();
            data_ = other.data_;
            block_allocator_ = other.block_allocator_;
            owns_block_ = other.owns_block_;
            other.data_ = nullptr;
            other.block_allocator_ = nullptr;
            other.owns_block_ = false;
        }
        return *this;
    }
    
    ~MessageBlockPtr() {
        reset();
    }
    
    void reset() {
        if (owns_block_ && block_allocator_ && data_) {
            block_allocator_->deallocate(data_);
        }
        data_ = nullptr;
        block_allocator_ = nullptr;
        owns_block_ = false;
    }
    
    void release() {
        owns_block_ = false;
    }
    
    T* get() const { return data_; }
    
    T& operator*() const { return *data_; }
    
    T* operator->() const { return data_; }
    
    explicit operator bool() const { return data_ != nullptr; }
    
    BlockAllocator* get_block_allocator() const { return block_allocator_; }
    
    bool owns_block() const { return owns_block_; }
    
    template<typename... Args>
    static MessageBlockPtr<T> make(BlockAllocator* block_allocator) {
        if (!block_allocator) {
            return MessageBlockPtr<T>();
        }
        
        void* raw = block_allocator->allocate(sizeof(T));
        if (!raw) {
            return MessageBlockPtr<T>();
        }
        
        T* data = new (raw) T();
        return MessageBlockPtr<T>(data, block_allocator);
    }
    
    template<typename... Args>
    static MessageBlockPtr<T> make(BlockAllocator* block_allocator, Args&&... args) {
        if (!block_allocator) {
            return MessageBlockPtr<T>();
        }
        
        void* raw = block_allocator->allocate(sizeof(T));
        if (!raw) {
            return MessageBlockPtr<T>();
        }
        
        T* data = new (raw) T(std::forward<Args>(args)...);
        return MessageBlockPtr<T>(data, block_allocator);
    }
    
    static MessageBlockPtr<T> wrap(T* data, BlockAllocator* block_allocator, bool owns_block = true) {
        return MessageBlockPtr<T>(data, block_allocator, owns_block);
    }
    
private:
    T* data_;
    BlockAllocator* block_allocator_;
    bool owns_block_;
};

template<typename T>
class LazyDestructBlockPtr {
public:
    LazyDestructBlockPtr() : data_(nullptr), block_allocator_(nullptr) {}
    
    LazyDestructBlockPtr(T* data, BlockAllocator* block_allocator)
        : data_(data), block_allocator_(block_allocator) {}
    
    LazyDestructBlockPtr(const LazyDestructBlockPtr&) = delete;
    LazyDestructBlockPtr& operator=(const LazyDestructBlockPtr&) = delete;
    
    LazyDestructBlockPtr(LazyDestructBlockPtr&& other) noexcept
        : data_(other.data_), block_allocator_(other.block_allocator_) {
        other.data_ = nullptr;
        other.block_allocator_ = nullptr;
    }
    
    LazyDestructBlockPtr& operator=(LazyDestructBlockPtr&& other) noexcept {
        if (this != &other) {
            reset();
            data_ = other.data_;
            block_allocator_ = other.block_allocator_;
            other.data_ = nullptr;
            other.block_allocator_ = nullptr;
        }
        return *this;
    }
    
    ~LazyDestructBlockPtr() {
        reset();
    }
    
    void reset() {
        if (block_allocator_ && data_) {
            block_allocator_->deallocate(data_);
        }
        data_ = nullptr;
        block_allocator_ = nullptr;
    }
    
    T* get() const { return data_; }
    
    T& operator*() const { return *data_; }
    
    T* operator->() const { return data_; }
    
    explicit operator bool() const { return data_ != nullptr; }
    
    template<typename... Args>
    static LazyDestructBlockPtr<T> allocate(BlockAllocator* block_allocator) {
        if (!block_allocator) {
            return LazyDestructBlockPtr<T>();
        }
        
        void* raw = block_allocator->allocate(sizeof(T));
        if (!raw) {
            return LazyDestructBlockPtr<T>();
        }
        
        T* data = new (raw) T();
        return LazyDestructBlockPtr<T>(data, block_allocator);
    }
    
    template<typename... Args>
    static LazyDestructBlockPtr<T> allocate(BlockAllocator* block_allocator, Args&&... args) {
        if (!block_allocator) {
            return LazyDestructBlockPtr<T>();
        }
        
        void* raw = block_allocator->allocate(sizeof(T));
        if (!raw) {
            return LazyDestructBlockPtr<T>();
        }
        
        T* data = new (raw) T(std::forward<Args>(args)...);
        return LazyDestructBlockPtr<T>(data, block_allocator);
    }
    
private:
    T* data_;
    BlockAllocator* block_allocator_;
};

} // namespace pangofly
