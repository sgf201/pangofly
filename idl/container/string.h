#pragma once

#include <cstddef>

#include "../allocator/block_allocator.h"

namespace pangofly {

class String {
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

    String();
    String(const char* s);
    String(const char* s, size_type count);
    String(size_type count, char c);
    String(const String& other);
    String(String&& other) noexcept;
    
    ~String();

    String& operator=(const char* s);
    String& operator=(const String& other);
    String& operator=(String&& other) noexcept;
    String& operator=(char c);

    String& assign(const char* s);
    String& assign(const char* s, size_type count);
    String& assign(const String& other);
    String& assign(String&& other) noexcept;
    String& assign(size_type count, char c);

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
    const_pointer c_str() const { return data_; }

    iterator begin() { return data_; }
    iterator end() { return data_ + size_; }
    const_iterator begin() const { return data_; }
    const_iterator end() const { return data_ + size_; }

    bool empty() const { return size_ == 0; }
    size_type size() const { return size_; }
    size_type length() const { return size_; }
    size_type capacity() const { return capacity_; }

    void reserve(size_type new_cap);
    void shrink_to_fit();

    void clear() { size_ = 0; }

    void push_back(char c);
    void pop_back();

    String& append(const char* s);
    String& append(const char* s, size_type count);
    String& append(const String& str);
    String& append(size_type count, char c);

    String& operator+=(const String& str);
    String& operator+=(const char* s);
    String& operator+=(char c);

    String substr(size_type pos = 0, size_type count = npos) const;
    String& replace(size_type pos, size_type count, const char* s);
    String& replace(size_type pos, size_type count, const String& str);

    size_type find(const char* s, size_type pos = 0) const;
    size_type find(const String& str, size_type pos = 0) const;
    size_type find(char c, size_type pos = 0) const;

    int compare(const String& str) const;
    int compare(const char* s) const;

    void resize(size_type count);
    void resize(size_type count, char c);

    void swap(String& other);

    void set_block_allocator(BlockAllocator* allocator) { allocator_ = allocator; }
    BlockAllocator* get_block_allocator() const { return allocator_; }

private:
    char* data_;
    size_type size_;
    size_type capacity_;
    BlockAllocator* allocator_;

    static size_t strlen_(const char* s);
};

inline String::String() 
    : data_(nullptr), size_(0), capacity_(0), allocator_(nullptr) {}

inline String::String(const char* s) 
    : data_(nullptr), size_(0), capacity_(0), allocator_(nullptr) {
    if (s) {
        assign(s);
    }
}

inline String::String(const char* s, size_type count) 
    : data_(nullptr), size_(0), capacity_(0), allocator_(nullptr) {
    assign(s, count);
}

inline String::String(size_type count, char c) 
    : data_(nullptr), size_(0), capacity_(0), allocator_(nullptr) {
    resize(count, c);
}

inline String::String(const String& other) 
    : data_(nullptr), size_(0), capacity_(0), allocator_(other.allocator_) {
    assign(other.data_, other.size_);
}

inline String::String(String&& other) noexcept 
    : data_(other.data_), size_(other.size_), 
      capacity_(other.capacity_), allocator_(other.allocator_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

inline String::~String() {
    if (data_ && allocator_) {
        allocator_->deallocate(data_);
    } else if (data_) {
        delete[] data_;
    }
}

inline String& String::operator=(const char* s) {
    return assign(s);
}

inline String& String::operator=(const String& other) {
    return assign(other);
}

inline String& String::operator=(String&& other) noexcept {
    if (this != &other) {
        if (data_ && allocator_) {
            allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        allocator_ = other.allocator_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

inline String& String::operator=(char c) {
    resize(1);
    data_[0] = c;
    return *this;
}

inline String& String::assign(const char* s) {
    size_type len = strlen_(s);
    return assign(s, len);
}

inline String& String::assign(const char* s, size_type count) {
    if (capacity_ < count + 1) {
        if (data_ && allocator_) {
            allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        if (allocator_) {
            data_ = static_cast<char*>(allocator_->allocate(count + 1));
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

inline String& String::assign(const String& other) {
    return assign(other.data_, other.size_);
}

inline String& String::assign(String&& other) noexcept {
    return *this = std::move(other);
}

inline String& String::assign(size_type count, char c) {
    if (capacity_ < count + 1) {
        if (data_ && allocator_) {
            allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        if (allocator_) {
            data_ = static_cast<char*>(allocator_->allocate(count + 1));
        } else {
            data_ = new char[count + 1];
        }
        capacity_ = count;
    }
    for (size_type i = 0; i < count; ++i) {
        data_[i] = c;
    }
    data_[count] = '\0';
    size_ = count;
    return *this;
}

inline String::reference String::at(size_type pos) {
    if (pos >= size_) {
        while (true) {}
    }
    return data_[pos];
}

inline String::const_reference String::at(size_type pos) const {
    if (pos >= size_) {
        while (true) {}
    }
    return data_[pos];
}

inline String::reference String::operator[](size_type pos) {
    return data_[pos];
}

inline String::const_reference String::operator[](size_type pos) const {
    return data_[pos];
}

inline String::reference String::front() {
    return data_[0];
}

inline String::const_reference String::front() const {
    return data_[0];
}

inline String::reference String::back() {
    return data_[size_ - 1];
}

inline String::const_reference String::back() const {
    return data_[size_ - 1];
}

inline void String::reserve(size_type new_cap) {
    if (new_cap > capacity_) {
        char* new_data = nullptr;
        if (allocator_) {
            new_data = static_cast<char*>(allocator_->allocate(new_cap + 1));
        } else {
            new_data = new char[new_cap + 1];
        }
        for (size_type i = 0; i < size_; ++i) {
            new_data[i] = data_[i];
        }
        new_data[size_] = '\0';
        if (data_ && allocator_) {
            allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        data_ = new_data;
        capacity_ = new_cap;
    }
}

inline void String::shrink_to_fit() {
    if (capacity_ > size_) {
        char* new_data = nullptr;
        if (allocator_) {
            new_data = static_cast<char*>(allocator_->allocate(size_ + 1));
        } else {
            new_data = new char[size_ + 1];
        }
        for (size_type i = 0; i < size_; ++i) {
            new_data[i] = data_[i];
        }
        new_data[size_] = '\0';
        if (data_ && allocator_) {
            allocator_->deallocate(data_);
        } else if (data_) {
            delete[] data_;
        }
        data_ = new_data;
        capacity_ = size_;
    }
}

inline void String::push_back(char c) {
    if (size_ >= capacity_) {
        reserve(capacity_ == 0 ? 1 : capacity_ * 2);
    }
    data_[size_++] = c;
    data_[size_] = '\0';
}

inline void String::pop_back() {
    if (size_ > 0) {
        --size_;
        data_[size_] = '\0';
    }
}

inline String& String::append(const char* s) {
    size_type len = strlen_(s);
    return append(s, len);
}

inline String& String::append(const char* s, size_type count) {
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

inline String& String::append(const String& str) {
    return append(str.data_, str.size_);
}

inline String& String::append(size_type count, char c) {
    for (size_type i = 0; i < count; ++i) {
        push_back(c);
    }
    return *this;
}

inline String& String::operator+=(const String& str) {
    return append(str);
}

inline String& String::operator+=(const char* s) {
    return append(s);
}

inline String& String::operator+=(char c) {
    push_back(c);
    return *this;
}

inline String String::substr(size_type pos, size_type count) const {
    if (pos > size_) {
        while (true) {}
    }
    size_type len = (count > size_ - pos) ? (size_ - pos) : count;
    return String(data_ + pos, len);
}

inline String& String::replace(size_type pos, size_type count, const char* s) {
    size_type len = strlen_(s);
    if (pos + count > size_) {
        while (true) {}
    }
    if (capacity_ < size_ - count + len) {
        reserve(size_ - count + len);
    }
    for (size_type i = pos + count; i < size_; ++i) {
        data_[i - count + len] = data_[i];
    }
    for (size_type i = 0; i < len; ++i) {
        data_[pos + i] = s[i];
    }
    size_ = size_ - count + len;
    data_[size_] = '\0';
    return *this;
}

inline String& String::replace(size_type pos, size_type count, const String& str) {
    return replace(pos, count, str.data_);
}

inline String::size_type String::find(const char* s, size_type pos) const {
    size_type len = strlen_(s);
    
    if (pos + len > size_) return npos;
    
    for (size_type i = pos; i <= size_ - len; ++i) {
        bool match = true;
        for (size_type j = 0; j < len; ++j) {
            if (data_[i + j] != s[j]) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return npos;
}

inline String::size_type String::find(const String& str, size_type pos) const {
    return find(str.data_, pos);
}

inline String::size_type String::find(char c, size_type pos) const {
    for (size_type i = pos; i < size_; ++i) {
        if (data_[i] == c) return i;
    }
    return npos;
}

inline int String::compare(const String& str) const {
    size_type len = (size_ < str.size_) ? size_ : str.size_;
    for (size_type i = 0; i < len; ++i) {
        if (data_[i] < str.data_[i]) return -1;
        if (data_[i] > str.data_[i]) return 1;
    }
    if (size_ < str.size_) return -1;
    if (size_ > str.size_) return 1;
    return 0;
}

inline int String::compare(const char* s) const {
    size_type len = strlen_(s);
    size_type cmp_len = (size_ < len) ? size_ : len;
    for (size_type i = 0; i < cmp_len; ++i) {
        if (data_[i] < s[i]) return -1;
        if (data_[i] > s[i]) return 1;
    }
    if (size_ < len) return -1;
    if (size_ > len) return 1;
    return 0;
}

inline void String::resize(size_type count) {
    resize(count, '\0');
}

inline void String::resize(size_type count, char c) {
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

inline void String::swap(String& other) {
    char* temp_data = data_;
    data_ = other.data_;
    other.data_ = temp_data;
    
    size_type temp_size = size_;
    size_ = other.size_;
    other.size_ = temp_size;
    
    size_type temp_cap = capacity_;
    capacity_ = other.capacity_;
    other.capacity_ = temp_cap;
    
    BlockAllocator* temp_alloc = allocator_;
    allocator_ = other.allocator_;
    other.allocator_ = temp_alloc;
}

inline size_t String::strlen_(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

inline bool operator==(const String& a, const String& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

inline bool operator!=(const String& a, const String& b) {
    return !(a == b);
}

inline bool operator<(const String& a, const String& b) {
    return a.compare(b) < 0;
}

inline bool operator<=(const String& a, const String& b) {
    return a.compare(b) <= 0;
}

inline bool operator>(const String& a, const String& b) {
    return a.compare(b) > 0;
}

inline bool operator>=(const String& a, const String& b) {
    return a.compare(b) >= 0;
}

inline String operator+(const String& a, const String& b) {
    String result;
    result.reserve(a.size() + b.size());
    result.append(a.c_str(), a.size());
    result.append(b.c_str(), b.size());
    return result;
}

inline String operator+(const String& a, const char* b) {
    if (!b) return a;
    String result;
    size_t b_len = 0;
    while (b[b_len] != '\0') b_len++;
    result.reserve(a.size() + b_len);
    result.append(a.c_str(), a.size());
    result.append(b, b_len);
    return result;
}

inline String operator+(const char* a, const String& b) {
    if (!a) return b;
    String result;
    size_t a_len = 0;
    while (a[a_len] != '\0') a_len++;
    result.reserve(a_len + b.size());
    result.append(a, a_len);
    result.append(b.c_str(), b.size());
    return result;
}

} // namespace pangofly
