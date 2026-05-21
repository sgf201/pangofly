#pragma once

#include <cstddef>
#include <iterator>
#include <algorithm>
#include <initializer_list>

#include "../allocator/block_allocator.h"

namespace pangofly {

template<typename T>
class MessageVector {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;
    using size_type = size_t;

    MessageVector();
    explicit MessageVector(BlockAllocator* block_allocator);
    explicit MessageVector(size_type count);
    MessageVector(size_type count, const T& value);
    
    MessageVector(const MessageVector&) = delete;
    MessageVector& operator=(const MessageVector&) = delete;
    
    MessageVector(MessageVector&& other) noexcept;
    MessageVector& operator=(MessageVector&& other) noexcept;
    
    ~MessageVector();

    void assign(size_type count, const T& value);
    
    reference operator[](size_type pos);
    const_reference operator[](size_type pos) const;

    reference front();
    const_reference front() const;

    reference back();
    const_reference back() const;

    pointer data() { return data_; }
    const_pointer data() const { return data_; }

    iterator begin() { return data_; }
    iterator end() { return data_ + size_; }
    const_iterator begin() const { return data_; }
    const_iterator end() const { return data_ + size_; }

    bool empty() const { return size_ == 0; }
    size_type size() const { return size_; }
    size_type capacity() const { return capacity_; }

    void reserve(size_type new_cap);
    void shrink_to_fit();

    void clear();

    void push_back(const T& value);
    void push_back(T&& value);
    
    template<typename... Args>
    reference emplace_back(Args&&... args);

    void pop_back();

    void resize(size_type count);
    void resize(size_type count, const T& value);

    void swap(MessageVector& other) noexcept;

    void set_block_allocator(BlockAllocator* allocator) { block_allocator_ = allocator; }
    BlockAllocator* get_block_allocator() const { return block_allocator_; }
    
    void release_block();

private:
    T* data_;
    size_type size_;
    size_type capacity_;
    BlockAllocator* block_allocator_;
    
    void allocate(size_type n);
    void deallocate();
};

template<typename T>
MessageVector<T>::MessageVector() 
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(nullptr) {}

template<typename T>
MessageVector<T>::MessageVector(BlockAllocator* block_allocator)
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(block_allocator) {}

template<typename T>
MessageVector<T>::MessageVector(size_type count)
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(nullptr) {
    resize(count);
}

template<typename T>
MessageVector<T>::MessageVector(size_type count, const T& value)
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(nullptr) {
    resize(count, value);
}

template<typename T>
MessageVector<T>::MessageVector(MessageVector&& other) noexcept
    : data_(other.data_), size_(other.size_), 
      capacity_(other.capacity_), block_allocator_(other.block_allocator_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

template<typename T>
MessageVector<T>& MessageVector<T>::operator=(MessageVector&& other) noexcept {
    if (this != &other) {
        clear();
        deallocate();
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        block_allocator_ = other.block_allocator_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

template<typename T>
MessageVector<T>::~MessageVector() {
    clear();
    deallocate();
}

template<typename T>
void MessageVector<T>::assign(size_type count, const T& value) {
    clear();
    resize(count, value);
}

template<typename T>
typename MessageVector<T>::reference 
MessageVector<T>::operator[](size_type pos) {
    return data_[pos];
}

template<typename T>
typename MessageVector<T>::const_reference 
MessageVector<T>::operator[](size_type pos) const {
    return data_[pos];
}

template<typename T>
typename MessageVector<T>::reference 
MessageVector<T>::front() {
    return data_[0];
}

template<typename T>
typename MessageVector<T>::const_reference 
MessageVector<T>::front() const {
    return data_[0];
}

template<typename T>
typename MessageVector<T>::reference 
MessageVector<T>::back() {
    return data_[size_ - 1];
}

template<typename T>
typename MessageVector<T>::const_reference 
MessageVector<T>::back() const {
    return data_[size_ - 1];
}

template<typename T>
void MessageVector<T>::reserve(size_type new_cap) {
    if (new_cap <= capacity_) return;
    
    T* new_data = nullptr;
    if (block_allocator_) {
        new_data = static_cast<T*>(block_allocator_->allocate(new_cap * sizeof(T)));
    } else {
        new_data = new T[new_cap];
    }
    
    for (size_type i = 0; i < size_; ++i) {
        new (new_data + i) T(std::move(data_[i]));
    }
    
    deallocate();
    
    data_ = new_data;
    capacity_ = new_cap;
}

template<typename T>
void MessageVector<T>::shrink_to_fit() {
    if (size_ == capacity_) return;
    
    T* new_data = nullptr;
    if (block_allocator_) {
        new_data = static_cast<T*>(block_allocator_->allocate(size_ * sizeof(T)));
    } else {
        new_data = new T[size_];
    }
    
    for (size_type i = 0; i < size_; ++i) {
        new (new_data + i) T(std::move(data_[i]));
    }
    
    deallocate();
    
    data_ = new_data;
    capacity_ = size_;
}

template<typename T>
void MessageVector<T>::clear() {
    for (size_type i = 0; i < size_; ++i) {
        data_[i].~T();
    }
    size_ = 0;
}

template<typename T>
void MessageVector<T>::push_back(const T& value) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(value);
    ++size_;
}

template<typename T>
void MessageVector<T>::push_back(T&& value) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(std::move(value));
    ++size_;
}

template<typename T>
template<typename... Args>
typename MessageVector<T>::reference 
MessageVector<T>::emplace_back(Args&&... args) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(std::forward<Args>(args)...);
    ++size_;
    return data_[size_ - 1];
}

template<typename T>
void MessageVector<T>::pop_back() {
    if (size_ > 0) {
        --size_;
        data_[size_].~T();
    }
}

template<typename T>
void MessageVector<T>::resize(size_type count) {
    resize(count, T{});
}

template<typename T>
void MessageVector<T>::resize(size_type count, const T& value) {
    if (count < size_) {
        for (size_type i = count; i < size_; ++i) {
            data_[i].~T();
        }
    } else if (count > size_) {
        reserve(count);
        for (size_type i = size_; i < count; ++i) {
            new (data_ + i) T(value);
        }
    }
    size_ = count;
}

template<typename T>
void MessageVector<T>::swap(MessageVector& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
    std::swap(block_allocator_, other.block_allocator_);
}

template<typename T>
void MessageVector<T>::release_block() {
    if (data_ && block_allocator_) {
        block_allocator_->deallocate_block(data_);
        data_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }
}

template<typename T>
void MessageVector<T>::allocate(size_type n) {
    if (n > 0) {
        if (block_allocator_) {
            data_ = static_cast<T*>(block_allocator_->allocate(n * sizeof(T)));
        } else {
            data_ = new T[n];
        }
        capacity_ = n;
    }
}

template<typename T>
void MessageVector<T>::deallocate() {
    if (data_) {
        if (block_allocator_) {
            block_allocator_->deallocate(data_);
        } else {
            delete[] data_;
        }
        data_ = nullptr;
        capacity_ = 0;
    }
}

class MessageString {
public:
    using value_type = char;
    using pointer = char*;
    using const_pointer = const char*;
    using reference = char&;
    using const_reference = const char&;
    using iterator = char*;
    using const_iterator = const char*;
    using size_type = size_t;

    static const size_type npos = static_cast<size_type>(-1);

    MessageString();
    MessageString(const char* s);
    MessageString(const char* s, size_type count);
    MessageString(size_type count, char c);
    
    MessageString(const MessageString&) = delete;
    MessageString& operator=(const MessageString&) = delete;
    
    MessageString(MessageString&& other) noexcept;
    MessageString& operator=(MessageString&& other) noexcept;
    
    ~MessageString();

    MessageString& assign(const char* s);
    MessageString& assign(const char* s, size_type count);

    reference operator[](size_type pos);
    const_reference operator[](size_type pos) const;

    const_pointer c_str() const { return data_; }
    pointer data() { return data_; }
    const_pointer data() const { return data_; }

    bool empty() const { return size_ == 0; }
    size_type size() const { return size_; }
    size_type length() const { return size_; }
    size_type capacity() const { return capacity_; }

    void reserve(size_type new_cap);
    void shrink_to_fit();

    void clear() { size_ = 0; }

    void push_back(char c);
    void pop_back();

    MessageString& append(const char* s);
    MessageString& append(const char* s, size_type count);
    MessageString& append(size_type count, char c);

    MessageString& operator+=(const char* s);
    MessageString& operator+=(char c);

    MessageString substr(size_type pos = 0, size_type count = npos) const;

    void resize(size_type count);
    void resize(size_type count, char c);

    void swap(MessageString& other);
    
    void set_block_allocator(BlockAllocator* allocator) { block_allocator_ = allocator; }
    BlockAllocator* get_block_allocator() const { return block_allocator_; }
    
    void release_block();

private:
    char* data_;
    size_type size_;
    size_type capacity_;
    BlockAllocator* block_allocator_;

    static size_t strlen_(const char* s);
};

inline MessageString::MessageString() 
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(nullptr) {}

inline MessageString::MessageString(const char* s) 
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(nullptr) {
    if (s) {
        assign(s);
    }
}

inline MessageString::MessageString(const char* s, size_type count) 
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(nullptr) {
    assign(s, count);
}

inline MessageString::MessageString(size_type count, char c) 
    : data_(nullptr), size_(0), capacity_(0), block_allocator_(nullptr) {
    resize(count, c);
}

inline MessageString::MessageString(MessageString&& other) noexcept 
    : data_(other.data_), size_(other.size_), 
      capacity_(other.capacity_), block_allocator_(other.block_allocator_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

inline MessageString& MessageString::operator=(MessageString&& other) noexcept {
    if (this != &other) {
        if (data_ && block_allocator_) {
            block_allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        block_allocator_ = other.block_allocator_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

inline MessageString::~MessageString() {
    if (data_) {
        if (block_allocator_) {
            block_allocator_->deallocate(data_);
        } else {
            delete[] data_;
        }
    }
}

inline MessageString& MessageString::assign(const char* s) {
    size_type len = strlen_(s);
    return assign(s, len);
}

inline MessageString& MessageString::assign(const char* s, size_type count) {
    if (capacity_ < count + 1) {
        if (data_ && block_allocator_) {
            block_allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        if (block_allocator_) {
            data_ = static_cast<char*>(block_allocator_->allocate(count + 1));
        } else {
            data_ = new char[count + 1];
        }
        capacity_ = count;
    }
    for (size_type i = 0; i < count; ++i) {
        data_[i] = s[i];
    }
    data_[count] = '\0';
    size_ = count;
    return *this;
}

inline MessageString::reference MessageString::operator[](size_type pos) {
    return data_[pos];
}

inline MessageString::const_reference MessageString::operator[](size_type pos) const {
    return data_[pos];
}

inline void MessageString::reserve(size_type new_cap) {
    if (new_cap > capacity_) {
        char* new_data = nullptr;
        if (block_allocator_) {
            new_data = static_cast<char*>(block_allocator_->allocate(new_cap + 1));
        } else {
            new_data = new char[new_cap + 1];
        }
        for (size_type i = 0; i < size_; ++i) {
            new_data[i] = data_[i];
        }
        new_data[size_] = '\0';
        if (data_ && block_allocator_) {
            block_allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        data_ = new_data;
        capacity_ = new_cap;
    }
}

inline void MessageString::shrink_to_fit() {
    if (capacity_ > size_) {
        char* new_data = nullptr;
        if (block_allocator_) {
            new_data = static_cast<char*>(block_allocator_->allocate(size_ + 1));
        } else {
            new_data = new char[size_ + 1];
        }
        for (size_type i = 0; i < size_; ++i) {
            new_data[i] = data_[i];
        }
        new_data[size_] = '\0';
        if (data_ && block_allocator_) {
            block_allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        data_ = new_data;
        capacity_ = size_;
    }
}

inline void MessageString::push_back(char c) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    data_[size_++] = c;
    data_[size_] = '\0';
}

inline void MessageString::pop_back() {
    if (size_ > 0) {
        --size_;
        data_[size_] = '\0';
    }
}

inline MessageString& MessageString::append(const char* s) {
    size_type len = strlen_(s);
    return append(s, len);
}

inline MessageString& MessageString::append(const char* s, size_type count) {
    size_type old_size = size_;
    if (capacity_ < old_size + count) {
        size_type new_cap = capacity_ == 0 ? count : capacity_ * 2;
        while (new_cap < old_size + count) new_cap *= 2;
        reserve(new_cap);
    }
    for (size_type i = 0; i < count; ++i) {
        data_[old_size + i] = s[i];
    }
    size_ = old_size + count;
    data_[size_] = '\0';
    return *this;
}

inline MessageString& MessageString::append(size_type count, char c) {
    for (size_type i = 0; i < count; ++i) {
        push_back(c);
    }
    return *this;
}

inline MessageString& MessageString::operator+=(const char* s) {
    return append(s);
}

inline MessageString& MessageString::operator+=(char c) {
    push_back(c);
    return *this;
}

inline MessageString MessageString::substr(size_type pos, size_type count) const {
    if (pos > size_) {
        while (true) {}
    }
    size_type len = (count > size_ - pos) ? (size_ - pos) : count;
    return MessageString(data_ + pos, len);
}

inline void MessageString::resize(size_type count) {
    resize(count, '\0');
}

inline void MessageString::resize(size_type count, char c) {
    if (count > size_) {
        if (count > capacity_) {
            reserve(count);
        }
        for (size_type i = size_; i < count; ++i) {
            data_[i] = c;
        }
    }
    size_ = count;
    data_[size_] = '\0';
}

inline void MessageString::swap(MessageString& other) {
    char* temp_data = data_;
    data_ = other.data_;
    other.data_ = temp_data;
    
    size_type temp_size = size_;
    size_ = other.size_;
    other.size_ = temp_size;
    
    size_type temp_cap = capacity_;
    capacity_ = other.capacity_;
    other.capacity_ = temp_cap;
    
    BlockAllocator* temp_alloc = block_allocator_;
    block_allocator_ = other.block_allocator_;
    other.block_allocator_ = temp_alloc;
}

inline void MessageString::release_block() {
    if (data_ && block_allocator_) {
        block_allocator_->deallocate_block(data_);
        data_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }
}

inline size_t MessageString::strlen_(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

} // namespace pangofly
