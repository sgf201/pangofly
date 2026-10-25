#ifndef PANGOFLY_V2_ALLOCATOR_H_
#define PANGOFLY_V2_ALLOCATOR_H_

// ============================================================================
// shm::Allocator / shm::vector / shm::string — 显式绑定 Arena 的容器
//
// 与旧 pmr 方案的根本差异：
//   * Allocator 默认构造被 delete → 任何「忘记绑定 arena」的用法都是编译错误
//   * arena 是成员变量，随对象走，不依赖 thread_local
//   * 分配失败抛 std::bad_alloc（符合 allocator 契约）
//
// shm::vector<T> 直接继承 std::vector<T, Allocator<T>>，因此 push_back /
// resize / operator[] / range-for 全部与标准容器一致，只是构造时必须给 arena：
//
//     shm::vector<Point> pts{arena};   // 或 Msg(Arena& a) : pts(a) {}
//     pts.push_back(...);
//
// 块回收不做逐元素析构，而是 arena->Reset()，所以消息类型必须平凡析构。
// 由于 shm::vector 不持有堆内存之外的资源，且 Allocator 平凡析构，
// 这一条件会自动成立。
// ============================================================================

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "core/shm/arena.h"

namespace pangofly {
namespace shm {

template <typename T>
class Allocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using is_always_equal = std::false_type;

  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  template <typename U>
  struct rebind {
    using other = Allocator<U>;
  };

  // 核心约束：没有 arena 就不能构造
  Allocator() = delete;

  explicit Allocator(Arena* a) noexcept : arena_(a) {}

  template <typename U>
  Allocator(const Allocator<U>& other) noexcept : arena_(other.arena()) {}

  pointer allocate(size_type n) {
    if (n == 0) return nullptr;
    if (arena_ == nullptr) throw std::bad_alloc{};
    void* p = arena_->Allocate(n * sizeof(T), alignof(T));
    if (p == nullptr) throw std::bad_alloc{};
    return static_cast<pointer>(p);
  }

  void deallocate(pointer p, size_type n) noexcept {
    if (p != nullptr && arena_ != nullptr) {
      arena_->Deallocate(p, n * sizeof(T));
    }
  }

  Arena* arena() const noexcept { return arena_; }

  template <typename U>
  bool operator==(const Allocator<U>& o) const noexcept {
    return arena_ == o.arena();
  }
  template <typename U>
  bool operator!=(const Allocator<U>& o) const noexcept {
    return arena_ != o.arena();
  }

 private:
  Arena* arena_;
};

// ---------------------------------------------------------------------------
// vector：std::vector 的子类，只是把 allocator 变成构造期必填参数
// ---------------------------------------------------------------------------
template <typename T>
class vector : public std::vector<T, Allocator<T>> {
  using Base = std::vector<T, Allocator<T>>;

 public:
  using allocator_type = Allocator<T>;

  explicit vector(Arena& a) noexcept : Base(allocator_type(&a)) {}
  explicit vector(Arena* a) noexcept : Base(allocator_type(a)) {}

  vector(Arena& a, size_t n) : Base(n, allocator_type(&a)) {}
  vector(Arena& a, size_t n, const T& v) : Base(n, v, allocator_type(&a)) {}

  template <typename It>
  vector(Arena& a, It first, It last) : Base(first, last, allocator_type(&a)) {}

  vector(const vector& o) : Base(static_cast<const Base&>(o)) {}
  vector(vector&& o) noexcept : Base(static_cast<Base&&>(o)) {}

  vector& operator=(const vector& o) {
    Base::operator=(static_cast<const Base&>(o));
    return *this;
  }
  vector& operator=(vector&& o) noexcept {
    Base::operator=(static_cast<Base&&>(o));
    return *this;
  }
};

// ---------------------------------------------------------------------------
// string：同理。libstdc++ 的 SSO 指针指向对象自身的内部缓冲，因同地址映射
// 依然跨进程有效。
// ---------------------------------------------------------------------------
class string : public std::basic_string<char, std::char_traits<char>, Allocator<char>> {
  using Base = std::basic_string<char, std::char_traits<char>, Allocator<char>>;

 public:
  explicit string(Arena& a) noexcept : Base(Allocator<char>(&a)) {}
  explicit string(Arena& a, const char* s) : Base(s, Allocator<char>(&a)) {}
  string(Arena& a, const char* s, size_t n)
      : Base(s, n, Allocator<char>(&a)) {}

  string(const string& o) : Base(static_cast<const Base&>(o)) {}
  string(string&& o) noexcept : Base(static_cast<Base&&>(o)) {}

  string& operator=(const string& o) {
    Base::operator=(static_cast<const Base&>(o));
    return *this;
  }
  string& operator=(string&& o) noexcept {
    Base::operator=(static_cast<Base&&>(o));
    return *this;
  }
};

// ---------------------------------------------------------------------------
// 编译期约束
//
// 消息对象永远不会被析构——整块内存由 arena->Reset() 一次性回收。
// 因此对类型的要求不是「平凡析构」，而是「析构函数无事可做」。
// 这一条件无法对任意用户类型自动判定，于是提供扩展点：
//   * 标量与平凡析构的聚合类型：自动通过
//   * shm::vector<T> / shm::string：递归检查 T
//   * 用户自定义消息：用 PANGOFLY_SHM_MESSAGE(Type) 声明，并对每个成员
//     自行确认其析构无事可做（IDL 生成的类型会自动带上这一声明）
// ---------------------------------------------------------------------------
template <typename T>
struct IsShmCompatible
    : std::integral_constant<bool,
                             std::is_trivially_destructible<T>::value &&
                                 !std::is_pointer<T>::value &&
                                 !std::is_member_pointer<T>::value &&
                                 !std::is_function<T>::value> {};

template <typename T>
struct IsShmCompatible<vector<T>> : IsShmCompatible<T> {};

template <>
struct IsShmCompatible<string> : std::true_type {};

template <typename T>
constexpr bool IsShmCompatibleV = IsShmCompatible<T>::value;

template <typename T>
constexpr void AssertShmCompatible() {
  static_assert(IsShmCompatibleV<T>,
                "shared-memory message type must be declared shm-compatible: "
                "use PANGOFLY_SHM_MESSAGE(Type) after the definition, and make sure "
                "every member's destructor has nothing to do");
}

}  // namespace shm
}  // namespace pangofly

// 在消息类型定义之后展开，声明该类型可用于共享内存
#define PANGOFLY_SHM_MESSAGE(...)                                     \
  namespace pangofly {                                                \
  namespace shm {                                                     \
  template <>                                                         \
  struct IsShmCompatible<__VA_ARGS__> : std::true_type {};            \
  }                                                                   \
  }

#endif  // PANGOFLY_V2_ALLOCATOR_H_
