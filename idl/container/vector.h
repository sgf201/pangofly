#pragma once

#include <cstddef>
#include <iterator>
#include <algorithm>
#include <initializer_list>
#include <type_traits>

#include "../allocator/block_allocator.h"

namespace pangofly {

template<typename T, typename Allocator = PangoflyAllocator<T>>
class Vector {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using allocator_type = Allocator;

    Vector();
    explicit Vector(BlockAllocator* block_allocator);
    explicit Vector(size_type count, const T& value = T{});
    Vector(size_type count, const T& value, BlockAllocator* block_allocator);

    Vector(const Vector& other);
    Vector(Vector&& other) noexcept;

    Vector(std::initializer_list<T> init);

    Vector(T* data, size_type size);

    ~Vector();

    Vector& operator=(const Vector& other);
    Vector& operator=(Vector&& other) noexcept;
    Vector& operator=(std::initializer_list<T> ilist);

    void assign(size_type count, const T& value);
    template<typename InputIt>
    void assign(InputIt first, InputIt last);
    void assign(std::initializer_list<T> ilist);

    reference at(size_type pos);
    const_reference at(size_type pos) const;

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

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

    bool empty() const { return size_ == 0; }
    size_type size() const { return size_; }
    size_type max_size() const { return size_type(-1) / sizeof(T); }
    size_type capacity() const { return capacity_; }

    void reserve(size_type new_cap);
    void shrink_to_fit();

    void clear() noexcept;

    iterator insert(const_iterator pos, const T& value);
    iterator insert(const_iterator pos, T&& value);
    iterator insert(const_iterator pos, size_type count, const T& value);
    template<typename InputIt>
    iterator insert(const_iterator pos, InputIt first, InputIt last);
    iterator insert(const_iterator pos, std::initializer_list<T> ilist);

    iterator erase(const_iterator pos);
    iterator erase(const_iterator first, const_iterator last);

    void push_back(const T& value);
    void push_back(T&& value);
    
    template<typename... Args>
    reference emplace_back(Args&&... args);

    void pop_back();

    void resize(size_type count);
    void resize(size_type count, const T& value);

    void swap(Vector& other) noexcept;

    BlockAllocator* get_block_allocator() const {
        return allocator_.get_block_allocator();
    }

    void set_block_allocator(BlockAllocator* block_allocator) {
        allocator_ = Allocator(block_allocator);
    }

    void copy_from_shm(const Vector& other, BlockAllocator* allocator);

private:
    T* data_;
    size_type size_;
    size_type capacity_;
    Allocator allocator_;
    
    void allocate(size_type n);
    void deallocate();
    void copy_data(const Vector& other);
};

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector() 
    : data_(nullptr), size_(0), capacity_(0), allocator_() {}

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector(BlockAllocator* block_allocator)
    : data_(nullptr), size_(0), capacity_(0), allocator_(block_allocator) {}

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector(size_type count, const T& value)
    : data_(nullptr), size_(0), capacity_(0), allocator_() {
    resize(count, value);
}

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector(size_type count, const T& value, BlockAllocator* block_allocator)
    : data_(nullptr), size_(0), capacity_(0), allocator_(block_allocator) {
    resize(count, value);
}

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector(const Vector& other)
    : data_(nullptr), size_(0), capacity_(0), allocator_(other.allocator_) {
    copy_data(other);
}

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector(Vector&& other) noexcept
    : data_(other.data_), size_(other.size_), 
      capacity_(other.capacity_), allocator_(std::move(other.allocator_)) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector(std::initializer_list<T> init)
    : data_(nullptr), size_(0), capacity_(0), allocator_() {
    reserve(init.size());
    for (const auto& elem : init) {
        push_back(elem);
    }
}

template<typename T, typename Allocator>
Vector<T, Allocator>::Vector(T* data, size_type size)
    : data_(data), size_(size), capacity_(size), allocator_() {
}

template<typename T, typename Allocator>
Vector<T, Allocator>::~Vector() {
    clear();
    deallocate();
}

template<typename T, typename Allocator>
Vector<T, Allocator>& Vector<T, Allocator>::operator=(const Vector& other) {
    if (this != &other) {
        clear();
        deallocate();
        allocator_ = other.allocator_;
        copy_data(other);
    }
    return *this;
}

template<typename T, typename Allocator>
Vector<T, Allocator>& Vector<T, Allocator>::operator=(Vector&& other) noexcept {
    if (this != &other) {
        clear();
        deallocate();
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        allocator_ = std::move(other.allocator_);
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

template<typename T, typename Allocator>
Vector<T, Allocator>& Vector<T, Allocator>::operator=(std::initializer_list<T> ilist) {
    clear();
    reserve(ilist.size());
    for (const auto& elem : ilist) {
        push_back(elem);
    }
    return *this;
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::assign(size_type count, const T& value) {
    clear();
    resize(count, value);
}

template<typename T, typename Allocator>
template<typename InputIt>
void Vector<T, Allocator>::assign(InputIt first, InputIt last) {
    clear();
    for (InputIt it = first; it != last; ++it) {
        push_back(*it);
    }
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::assign(std::initializer_list<T> ilist) {
    *this = ilist;
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::reference 
Vector<T, Allocator>::at(size_type pos) {
    if (pos >= size_) {
        while (true) {}
    }
    return data_[pos];
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::const_reference 
Vector<T, Allocator>::at(size_type pos) const {
    if (pos >= size_) {
        while (true) {}
    }
    return data_[pos];
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::reference 
Vector<T, Allocator>::operator[](size_type pos) {
    return data_[pos];
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::const_reference 
Vector<T, Allocator>::operator[](size_type pos) const {
    return data_[pos];
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::reference 
Vector<T, Allocator>::front() {
    return data_[0];
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::const_reference 
Vector<T, Allocator>::front() const {
    return data_[0];
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::reference 
Vector<T, Allocator>::back() {
    return data_[size_ - 1];
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::const_reference 
Vector<T, Allocator>::back() const {
    return data_[size_ - 1];
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::reserve(size_type new_cap) {
    if (new_cap <= capacity_) return;
    
    T* new_data = allocator_.allocate(new_cap);
    
    for (size_type i = 0; i < size_; ++i) {
        new (new_data + i) T(std::move(data_[i]));
        data_[i].~T();
    }
    
    if (data_) {
        allocator_.deallocate(data_, capacity_);
    }
    
    data_ = new_data;
    capacity_ = new_cap;
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::shrink_to_fit() {
    if (size_ == capacity_) return;
    
    T* new_data = allocator_.allocate(size_);
    
    for (size_type i = 0; i < size_; ++i) {
        new (new_data + i) T(std::move(data_[i]));
        data_[i].~T();
    }
    
    if (data_) {
        allocator_.deallocate(data_, capacity_);
    }
    
    data_ = new_data;
    capacity_ = size_;
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::clear() noexcept {
    for (size_type i = 0; i < size_; ++i) {
        data_[i].~T();
    }
    size_ = 0;
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::iterator 
Vector<T, Allocator>::insert(const_iterator pos, const T& value) {
    size_type idx = pos - begin();
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    
    for (size_type i = size_; i > idx; --i) {
        new (data_ + i) T(std::move(data_[i - 1]));
        data_[i - 1].~T();
    }
    
    new (data_ + idx) T(value);
    ++size_;
    return data_ + idx;
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::iterator 
Vector<T, Allocator>::insert(const_iterator pos, T&& value) {
    size_type idx = pos - begin();
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    
    for (size_type i = size_; i > idx; --i) {
        new (data_ + i) T(std::move(data_[i - 1]));
        data_[i - 1].~T();
    }
    
    new (data_ + idx) T(std::move(value));
    ++size_;
    return data_ + idx;
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::iterator 
Vector<T, Allocator>::insert(const_iterator pos, size_type count, const T& value) {
    size_type idx = pos - begin();
    if (size_ + count > capacity_) {
        reserve((size_ + count) * 2);
    }
    
    for (size_type i = size_ + count - 1; i >= idx + count; --i) {
        new (data_ + i) T(std::move(data_[i - count]));
        data_[i - count].~T();
    }
    
    for (size_type i = 0; i < count; ++i) {
        new (data_ + idx + i) T(value);
    }
    
    size_ += count;
    return data_ + idx;
}

template<typename T, typename Allocator>
template<typename InputIt>
typename Vector<T, Allocator>::iterator 
Vector<T, Allocator>::insert(const_iterator pos, InputIt first, InputIt last) {
    size_type count = std::distance(first, last);
    size_type idx = pos - begin();
    
    if (size_ + count > capacity_) {
        reserve((size_ + count) * 2);
    }
    
    for (size_type i = size_ + count - 1; i >= idx + count; --i) {
        new (data_ + i) T(std::move(data_[i - count]));
        data_[i - count].~T();
    }
    
    InputIt it = first;
    for (size_type i = 0; i < count; ++i, ++it) {
        new (data_ + idx + i) T(*it);
    }
    
    size_ += count;
    return data_ + idx;
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::iterator 
Vector<T, Allocator>::insert(const_iterator pos, std::initializer_list<T> ilist) {
    return insert(pos, ilist.begin(), ilist.end());
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::iterator 
Vector<T, Allocator>::erase(const_iterator pos) {
    size_type idx = pos - begin();
    
    data_[idx].~T();
    for (size_type i = idx; i < size_ - 1; ++i) {
        new (data_ + i) T(std::move(data_[i + 1]));
        data_[i + 1].~T();
    }
    
    --size_;
    return data_ + idx;
}

template<typename T, typename Allocator>
typename Vector<T, Allocator>::iterator 
Vector<T, Allocator>::erase(const_iterator first, const_iterator last) {
    size_type start = first - begin();
    size_type end = last - begin();
    size_type count = end - start;
    
    for (size_type i = start; i < end; ++i) {
        data_[i].~T();
    }
    
    for (size_type i = end; i < size_; ++i) {
        new (data_ + i - count) T(std::move(data_[i]));
        data_[i].~T();
    }
    
    size_ -= count;
    return data_ + start;
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::push_back(const T& value) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(value);
    ++size_;
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::push_back(T&& value) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(std::move(value));
    ++size_;
}

template<typename T, typename Allocator>
template<typename... Args>
typename Vector<T, Allocator>::reference 
Vector<T, Allocator>::emplace_back(Args&&... args) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(std::forward<Args>(args)...);
    ++size_;
    return data_[size_ - 1];
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::pop_back() {
    if (size_ > 0) {
        --size_;
        data_[size_].~T();
    }
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::resize(size_type count) {
    resize(count, T{});
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::resize(size_type count, const T& value) {
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

template<typename T, typename Allocator>
void Vector<T, Allocator>::swap(Vector& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
    std::swap(allocator_, other.allocator_);
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::allocate(size_type n) {
    if (n > 0) {
        data_ = allocator_.allocate(n);
        capacity_ = n;
    }
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::deallocate() {
    if (data_) {
        allocator_.deallocate(data_, capacity_);
        data_ = nullptr;
        capacity_ = 0;
    }
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::copy_data(const Vector& other) {
    reserve(other.size_);
    for (size_type i = 0; i < other.size_; ++i) {
        new (data_ + i) T(other.data_[i]);
    }
    size_ = other.size_;
}

template<typename T, typename Allocator>
void Vector<T, Allocator>::copy_from_shm(const Vector& other, BlockAllocator* allocator) {
    clear();
    if (other.empty()) return;
    
    set_block_allocator(allocator);
    reserve(other.size_);
    
    for (size_type i = 0; i < other.size_; ++i) {
        push_back(other.data_[i]);
    }
}

} // namespace pangofly
