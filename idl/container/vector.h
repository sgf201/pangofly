#pragma once

#include <cstddef>
#include <iterator>
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>

namespace pangofly {

template<typename T>
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

    Vector();
    explicit Vector(size_type count, const T& value = T{});
    
    Vector(const Vector& other);
    Vector(Vector&& other) noexcept;
    
    Vector(std::initializer_list<T> init);
    
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

    pointer data() noexcept { return data_; }
    const_pointer data() const noexcept { return data_; }

    iterator begin() noexcept { return data_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator cbegin() const noexcept { return data_; }

    iterator end() noexcept { return data_ + size_; }
    const_iterator end() const noexcept { return data_ + size_; }
    const_iterator cend() const noexcept { return data_ + size_; }

    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }

    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    size_type size() const noexcept { return size_; }
    size_type capacity() const noexcept { return capacity_; }

    size_type max_size() const noexcept {
        return static_cast<size_type>(-1) / sizeof(T);
    }

    void reserve(size_type new_cap);
    void shrink_to_fit();

    void clear() noexcept;

    iterator insert(iterator pos, const T& value);
    iterator insert(iterator pos, T&& value);
    iterator insert(iterator pos, size_type count, const T& value);
    template<typename InputIt>
    iterator insert(iterator pos, InputIt first, InputIt last);
    iterator insert(iterator pos, std::initializer_list<T> ilist);

    template<typename... Args>
    iterator emplace(iterator pos, Args&&... args);

    iterator erase(iterator pos);
    iterator erase(iterator first, iterator last);

    void push_back(const T& value);
    void push_back(T&& value);

    template<typename... Args>
    reference emplace_back(Args&&... args);

    void pop_back();

    void resize(size_type count);
    void resize(size_type count, const value_type& value);

    void swap(Vector& other) noexcept;

    bool is_shared() const { return shared_; }
    void set_shared(bool shared) { shared_ = shared; }

private:
    T* data_;
    size_type size_;
    size_type capacity_;
    bool shared_;
};

template<typename T>
Vector<T>::Vector() 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {}

template<typename T>
Vector<T>::Vector(size_type count, const T& value) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    resize(count, value);
}

template<typename T>
Vector<T>::Vector(const Vector& other) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    assign(other.begin(), other.end());
}

template<typename T>
Vector<T>::Vector(Vector&& other) noexcept 
    : data_(other.data_), size_(other.size_), 
      capacity_(other.capacity_), shared_(other.shared_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

template<typename T>
Vector<T>::Vector(std::initializer_list<T> init) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    assign(init);
}

template<typename T>
Vector<T>::~Vector() {
    if (data_ && !shared_) {
        clear();
        operator delete(data_);
    }
}

template<typename T>
Vector<T>& Vector<T>::operator=(const Vector& other) {
    if (this != &other) {
        assign(other.begin(), other.end());
    }
    return *this;
}

template<typename T>
Vector<T>& Vector<T>::operator=(Vector&& other) noexcept {
    if (this != &other) {
        if (data_ && !shared_) {
            clear();
            operator delete(data_);
        }
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        shared_ = other.shared_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

template<typename T>
Vector<T>& Vector<T>::operator=(std::initializer_list<T> ilist) {
    assign(ilist);
    return *this;
}

template<typename T>
void Vector<T>::assign(size_type count, const T& value) {
    clear();
    if (capacity_ < count) {
        if (data_ && !shared_) {
            operator delete(data_);
        }
        data_ = static_cast<T*>(operator new(count * sizeof(T)));
        capacity_ = count;
        shared_ = false;
    }
    for (size_type i = 0; i < count; ++i) {
        new (data_ + i) T(value);
    }
    size_ = count;
}

template<typename T>
template<typename InputIt>
void Vector<T>::assign(InputIt first, InputIt last) {
    clear();
    for (auto it = first; it != last; ++it) {
        push_back(*it);
    }
}

template<typename T>
void Vector<T>::assign(std::initializer_list<T> ilist) {
    assign(ilist.begin(), ilist.end());
}

template<typename T>
typename Vector<T>::reference Vector<T>::at(size_type pos) {
    if (!(pos < size_)) {
        throw std::out_of_range("Vector index out of range");
    }
    return data_[pos];
}

template<typename T>
typename Vector<T>::const_reference Vector<T>::at(size_type pos) const {
    if (!(pos < size_)) {
        throw std::out_of_range("Vector index out of range");
    }
    return data_[pos];
}

template<typename T>
typename Vector<T>::reference Vector<T>::operator[](size_type pos) {
    return data_[pos];
}

template<typename T>
typename Vector<T>::const_reference Vector<T>::operator[](size_type pos) const {
    return data_[pos];
}

template<typename T>
typename Vector<T>::reference Vector<T>::front() {
    return data_[0];
}

template<typename T>
typename Vector<T>::const_reference Vector<T>::front() const {
    return data_[0];
}

template<typename T>
typename Vector<T>::reference Vector<T>::back() {
    return data_[size_ - 1];
}

template<typename T>
typename Vector<T>::const_reference Vector<T>::back() const {
    return data_[size_ - 1];
}

template<typename T>
void Vector<T>::reserve(size_type new_cap) {
    if (new_cap > capacity_) {
        T* new_data = static_cast<T*>(operator new(new_cap * sizeof(T)));
        for (size_type i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        if (data_ && !shared_) {
            operator delete(data_);
        }
        data_ = new_data;
        capacity_ = new_cap;
        shared_ = false;
    }
}

template<typename T>
void Vector<T>::shrink_to_fit() {
    if (capacity_ > size_) {
        if (size_ == 0) {
            if (data_ && !shared_) {
                operator delete(data_);
            }
            data_ = nullptr;
            capacity_ = 0;
        } else {
            T* new_data = static_cast<T*>(operator new(size_ * sizeof(T)));
            for (size_type i = 0; i < size_; ++i) {
                new (new_data + i) T(std::move(data_[i]));
                data_[i].~T();
            }
            if (data_ && !shared_) {
                operator delete(data_);
            }
            data_ = new_data;
            capacity_ = size_;
            shared_ = false;
        }
    }
}

template<typename T>
void Vector<T>::clear() noexcept {
    for (size_type i = 0; i < size_; ++i) {
        data_[i].~T();
    }
    size_ = 0;
}

template<typename T>
typename Vector<T>::iterator Vector<T>::insert(iterator pos, const T& value) {
    return insert(pos, 1, value);
}

template<typename T>
typename Vector<T>::iterator Vector<T>::insert(iterator pos, T&& value) {
    size_type idx = pos - begin();
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    pos = begin() + idx;
    for (auto it = end(); it != pos; --it) {
        new (it) T(std::move(*(it - 1)));
        (it - 1)->~T();
    }
    new (pos) T(std::move(value));
    ++size_;
    return pos;
}

template<typename T>
typename Vector<T>::iterator Vector<T>::insert(iterator pos, size_type count, const T& value) {
    if (count == 0) return pos;
    
    size_type idx = pos - begin();
    size_type new_size = size_ + count;
    
    if (new_size > capacity_) {
        size_type new_cap = capacity_ == 0 ? count : capacity_ * 2;
        while (new_cap < new_size) new_cap *= 2;
        
        T* new_data = static_cast<T*>(operator new(new_cap * sizeof(T)));
        
        for (size_type i = 0; i < idx; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        for (size_type i = 0; i < count; ++i) {
            new (new_data + idx + i) T(value);
        }
        for (size_type i = idx; i < size_; ++i) {
            new (new_data + idx + count + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        
        if (data_ && !shared_) {
            operator delete(data_);
        }
        data_ = new_data;
        capacity_ = new_cap;
        shared_ = false;
    } else {
        for (size_type i = size_; i > idx; --i) {
            new (data_ + i + count - 1) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        for (size_type i = 0; i < count; ++i) {
            new (data_ + idx + i) T(value);
        }
    }
    
    size_ = new_size;
    return begin() + idx;
}

template<typename T>
template<typename InputIt>
typename Vector<T>::iterator Vector<T>::insert(iterator pos, InputIt first, InputIt last) {
    size_type count = std::distance(first, last);
    if (count == 0) return pos;
    
    size_type idx = pos - begin();
    size_type new_size = size_ + count;
    
    if (new_size > capacity_) {
        size_type new_cap = capacity_ == 0 ? count : capacity_ * 2;
        while (new_cap < new_size) new_cap *= 2;
        
        T* new_data = static_cast<T*>(operator new(new_cap * sizeof(T)));
        
        auto it = first;
        for (size_type i = 0; i < idx; ++i) {
            new (new_data + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        for (size_type i = 0; i < count; ++i, ++it) {
            new (new_data + idx + i) T(*it);
        }
        for (size_type i = idx; i < size_; ++i) {
            new (new_data + idx + count + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        
        if (data_ && !shared_) {
            operator delete(data_);
        }
        data_ = new_data;
        capacity_ = new_cap;
        shared_ = false;
    } else {
        for (size_type i = size_; i > idx; --i) {
            new (data_ + i + count - 1) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        auto it = first;
        for (size_type i = 0; i < count; ++i, ++it) {
            new (data_ + idx + i) T(*it);
        }
    }
    
    size_ = new_size;
    return begin() + idx;
}

template<typename T>
typename Vector<T>::iterator Vector<T>::insert(iterator pos, std::initializer_list<T> ilist) {
    return insert(pos, ilist.begin(), ilist.end());
}

template<typename T>
template<typename... Args>
typename Vector<T>::iterator Vector<T>::emplace(iterator pos, Args&&... args) {
    size_type idx = pos - begin();
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    pos = begin() + idx;
    for (auto it = end(); it != pos; --it) {
        new (it) T(std::move(*(it - 1)));
        (it - 1)->~T();
    }
    new (pos) T(std::forward<Args>(args)...);
    ++size_;
    return pos;
}

template<typename T>
typename Vector<T>::iterator Vector<T>::erase(iterator pos) {
    return erase(pos, pos + 1);
}

template<typename T>
typename Vector<T>::iterator Vector<T>::erase(iterator first, iterator last) {
    if (first == last) return first;
    
    size_type count = last - first;
    size_type idx = first - begin();
    
    for (auto it = last; it != end(); ++it) {
        new (it - count) T(std::move(*it));
        it->~T();
    }
    size_ -= count;
    return begin() + idx;
}

template<typename T>
void Vector<T>::push_back(const T& value) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(value);
    ++size_;
}

template<typename T>
void Vector<T>::push_back(T&& value) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(std::move(value));
    ++size_;
}

template<typename T>
template<typename... Args>
typename Vector<T>::reference Vector<T>::emplace_back(Args&&... args) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    new (data_ + size_) T(std::forward<Args>(args)...);
    ++size_;
    return back();
}

template<typename T>
void Vector<T>::pop_back() {
    --size_;
    data_[size_].~T();
}

template<typename T>
void Vector<T>::resize(size_type count) {
    resize(count, T{});
}

template<typename T>
void Vector<T>::resize(size_type count, const value_type& value) {
    if (count > size_) {
        if (count > capacity_) {
            reserve(count);
        }
        for (size_type i = size_; i < count; ++i) {
            new (data_ + i) T(value);
        }
    } else if (count < size_) {
        for (size_type i = count; i < size_; ++i) {
            data_[i].~T();
        }
    }
    size_ = count;
}

template<typename T>
void Vector<T>::swap(Vector& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
    std::swap(shared_, other.shared_);
}

template<typename T>
bool operator==(const Vector<T>& a, const Vector<T>& b) {
    return a.size() == b.size() && 
           std::equal(a.begin(), a.end(), b.begin());
}

template<typename T>
bool operator!=(const Vector<T>& a, const Vector<T>& b) {
    return !(a == b);
}

template<typename T>
bool operator<(const Vector<T>& a, const Vector<T>& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

template<typename T>
bool operator<=(const Vector<T>& a, const Vector<T>& b) {
    return !(b < a);
}

template<typename T>
bool operator>(const Vector<T>& a, const Vector<T>& b) {
    return b < a;
}

template<typename T>
bool operator>=(const Vector<T>& a, const Vector<T>& b) {
    return !(a < b);
}

template<typename T>
void swap(Vector<T>& a, Vector<T>& b) noexcept {
    a.swap(b);
}

} // namespace pangofly
