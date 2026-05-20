#pragma once

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <initializer_list>
#include <iostream>

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
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using size_type = size_t;

    static const size_type npos = static_cast<size_type>(-1);

    String();
    String(const char* s);
    String(const char* s, size_type count);
    String(size_type count, char c);
    String(const String& other);
    String(String&& other) noexcept;
    String(std::initializer_list<char> init);
    
    ~String();

    String& operator=(const char* s);
    String& operator=(const String& other);
    String& operator=(String&& other) noexcept;
    String& operator=(char c);
    String& operator=(std::initializer_list<char> ilist);

    String& assign(const char* s);
    String& assign(const char* s, size_type count);
    String& assign(const String& other);
    String& assign(String&& other) noexcept;
    String& assign(size_type count, char c);
    String& assign(std::initializer_list<char> ilist);

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
    const_iterator cbegin() const { return data_; }
    const_iterator cend() const { return data_ + size_; }

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const { return const_reverse_iterator(cend()); }
    const_reverse_iterator crend() const { return const_reverse_iterator(cbegin()); }

    [[nodiscard]] bool empty() const { return size_ == 0; }
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
    String& append(std::initializer_list<char> ilist);

    String& operator+=(const String& str);
    String& operator+=(const char* s);
    String& operator+=(char c);
    String& operator+=(std::initializer_list<char> ilist);

    String substr(size_type pos = 0, size_type count = npos) const;
    String& replace(size_type pos, size_type count, const char* s);
    String& replace(size_type pos, size_type count, const char* s, size_type count2);
    String& replace(size_type pos, size_type count, const String& str);
    String& replace(size_type pos, size_type count, size_type count2, char c);

    size_type find(const char* s, size_type pos = 0) const;
    size_type find(const String& str, size_type pos = 0) const;
    size_type find(char c, size_type pos = 0) const;

    size_type rfind(const char* s, size_type pos = npos) const;
    size_type rfind(const String& str, size_type pos = npos) const;
    size_type rfind(char c, size_type pos = npos) const;

    int compare(const String& str) const;
    int compare(const char* s) const;
    int compare(size_type pos, size_type count, const String& str) const;
    int compare(size_type pos, size_type count, const char* s) const;

    void resize(size_type count);
    void resize(size_type count, char c);

    void swap(String& other);

    bool is_shared() const { return shared_; }
    void set_shared(bool shared) { shared_ = shared; }

private:
    char* data_;
    size_type size_;
    size_type capacity_;
    bool shared_;
};

inline String::String() 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {}

inline String::String(const char* s) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    if (s) {
        size_type len = 0;
        while (s[len] != '\0') ++len;
        assign(s, len);
    }
}

inline String::String(const char* s, size_type count) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    assign(s, count);
}

inline String::String(size_type count, char c) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    resize(count, c);
}

inline String::String(const String& other) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    assign(other.data_, other.size_);
}

inline String::String(String&& other) noexcept 
    : data_(other.data_), size_(other.size_), 
      capacity_(other.capacity_), shared_(other.shared_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

inline String::String(std::initializer_list<char> init) 
    : data_(nullptr), size_(0), capacity_(0), shared_(false) {
    assign(init.begin(), init.size());
}

inline String::~String() {
    if (data_ && !shared_) {
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
        if (data_ && !shared_) {
            delete[] data_;
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

inline String& String::operator=(char c) {
    resize(1);
    data_[0] = c;
    return *this;
}

inline String& String::operator=(std::initializer_list<char> ilist) {
    return assign(ilist.begin(), ilist.size());
}

inline String& String::assign(const char* s) {
    size_type len = 0;
    while (s[len] != '\0') ++len;
    return assign(s, len);
}

inline String& String::assign(const char* s, size_type count) {
    if (capacity_ < count) {
        if (data_ && !shared_) {
            delete[] data_;
        }
        data_ = new char[count + 1];
        capacity_ = count;
        shared_ = false;
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
    if (capacity_ < count) {
        if (data_ && !shared_) {
            delete[] data_;
        }
        data_ = new char[count + 1];
        capacity_ = count;
        shared_ = false;
    }
    for (size_type i = 0; i < count; ++i) {
        data_[i] = c;
    }
    data_[count] = '\0';
    size_ = count;
    return *this;
}

inline String& String::assign(std::initializer_list<char> ilist) {
    return assign(ilist.begin(), ilist.size());
}

inline typename String::reference String::at(size_type pos) {
    if (pos >= size_) {
        throw std::out_of_range("String index out of range");
    }
    return data_[pos];
}

inline typename String::const_reference String::at(size_type pos) const {
    if (pos >= size_) {
        throw std::out_of_range("String index out of range");
    }
    return data_[pos];
}

inline typename String::reference String::operator[](size_type pos) {
    return data_[pos];
}

inline typename String::const_reference String::operator[](size_type pos) const {
    return data_[pos];
}

inline typename String::reference String::front() {
    return data_[0];
}

inline typename String::const_reference String::front() const {
    return data_[0];
}

inline typename String::reference String::back() {
    return data_[size_ - 1];
}

inline typename String::const_reference String::back() const {
    return data_[size_ - 1];
}

inline void String::reserve(size_type new_cap) {
    if (new_cap > capacity_) {
        char* new_data = new char[new_cap + 1];
        for (size_type i = 0; i < size_; ++i) {
            new_data[i] = data_[i];
        }
        new_data[size_] = '\0';
        if (data_ && !shared_) {
            delete[] data_;
        }
        data_ = new_data;
        capacity_ = new_cap;
        shared_ = false;
    }
}

inline void String::shrink_to_fit() {
    if (capacity_ > size_) {
        char* new_data = new char[size_ + 1];
        for (size_type i = 0; i < size_; ++i) {
            new_data[i] = data_[i];
        }
        new_data[size_] = '\0';
        if (data_ && !shared_) {
            delete[] data_;
        }
        data_ = new_data;
        capacity_ = size_;
        shared_ = false;
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
    size_type len = 0;
    while (s[len] != '\0') ++len;
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

inline String& String::append(std::initializer_list<char> ilist) {
    for (char c : ilist) {
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

inline String& String::operator+=(std::initializer_list<char> ilist) {
    return append(ilist);
}

inline String String::substr(size_type pos, size_type count) const {
    if (pos > size_) {
        throw std::out_of_range("String position out of range");
    }
    size_type len = std::min(count, size_ - pos);
    return String(data_ + pos, len);
}

inline String& String::replace(size_type pos, size_type count, const char* s) {
    size_type len = 0;
    while (s[len] != '\0') ++len;
    return replace(pos, count, s, len);
}

inline String& String::replace(size_type pos, size_type count, const char* s, size_type count2) {
    if (pos > size_) {
        throw std::out_of_range("String position out of range");
    }
    String temp;
    temp.reserve(size_ - count + count2);
    temp.append(data_, pos);
    temp.append(s, count2);
    temp.append(data_ + pos + count, size_ - pos - count);
    return *this = std::move(temp);
}

inline String& String::replace(size_type pos, size_type count, const String& str) {
    return replace(pos, count, str.data_, str.size_);
}

inline String& String::replace(size_type pos, size_type count, size_type count2, char c) {
    String s(count2, c);
    return replace(pos, count, s);
}

inline typename String::size_type String::find(const char* s, size_type pos) const {
    size_type len = 0;
    while (s[len] != '\0') ++len;
    
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

inline typename String::size_type String::find(const String& str, size_type pos) const {
    return find(str.data_, pos);
}

inline typename String::size_type String::find(char c, size_type pos) const {
    for (size_type i = pos; i < size_; ++i) {
        if (data_[i] == c) return i;
    }
    return npos;
}

inline typename String::size_type String::rfind(const char* s, size_type pos) const {
    size_type len = 0;
    while (s[len] != '\0') ++len;
    
    if (len > size_) return npos;
    
    size_type start = std::min(pos, size_ - len);
    for (size_type i = start + 1; i-- > 0; ) {
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

inline typename String::size_type String::rfind(const String& str, size_type pos) const {
    return rfind(str.data_, pos);
}

inline typename String::size_type String::rfind(char c, size_type pos) const {
    size_type start = std::min(pos, size_);
    for (size_type i = start; i-- > 0; ) {
        if (data_[i] == c) return i;
    }
    return npos;
}

inline int String::compare(const String& str) const {
    size_type len = std::min(size_, str.size_);
    for (size_type i = 0; i < len; ++i) {
        if (data_[i] < str.data_[i]) return -1;
        if (data_[i] > str.data_[i]) return 1;
    }
    if (size_ < str.size_) return -1;
    if (size_ > str.size_) return 1;
    return 0;
}

inline int String::compare(const char* s) const {
    size_type len = 0;
    while (s[len] != '\0') ++len;
    
    size_type cmp_len = std::min(size_, len);
    for (size_type i = 0; i < cmp_len; ++i) {
        if (data_[i] < s[i]) return -1;
        if (data_[i] > s[i]) return 1;
    }
    if (size_ < len) return -1;
    if (size_ > len) return 1;
    return 0;
}

inline int String::compare(size_type pos, size_type count, const String& str) const {
    return substr(pos, count).compare(str);
}

inline int String::compare(size_type pos, size_type count, const char* s) const {
    return substr(pos, count).compare(s);
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
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
    std::swap(shared_, other.shared_);
}

inline bool operator==(const String& a, const String& b) {
    return a.size() == b.size() && 
           std::strncmp(a.c_str(), b.c_str(), a.size()) == 0;
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
    result += a;
    result += b;
    return result;
}

inline String operator+(const String& a, const char* b) {
    String result;
    result.reserve(a.size() + std::strlen(b));
    result += a;
    result += b;
    return result;
}

inline String operator+(const char* a, const String& b) {
    String result;
    result.reserve(std::strlen(a) + b.size());
    result += a;
    result += b;
    return result;
}

inline void swap(String& a, String& b) {
    a.swap(b);
}

inline std::ostream& operator<<(std::ostream& os, const String& str) {
    os.write(str.c_str(), str.size());
    return os;
}

} // namespace pangofly
