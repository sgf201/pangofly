#ifndef PANGOFLY_MEMORY_PMR_H_
#define PANGOFLY_MEMORY_PMR_H_

// Compatibility shim: prefer the standard <memory_resource> header when
// available, fall back to the experimental implementation on older toolchains
// (e.g. GCC 8.3 with uclibc where only <experimental/memory_resource> exists).
//
// On the experimental path the container aliases (vector, string) are not
// provided by the standard library, so we define them here.

#if __has_include(<memory_resource>)
  #include <memory_resource>
  #include <vector>
  #include <string>
  namespace pangofly { namespace pmr = std::pmr; }
#elif __has_include(<experimental/memory_resource>)
  #include <experimental/memory_resource>
  #include <vector>
  #include <string>
  namespace pangofly {
    namespace pmr {
      using std::experimental::pmr::memory_resource;
      using std::experimental::pmr::polymorphic_allocator;
      using std::experimental::pmr::get_default_resource;
      using std::experimental::pmr::new_delete_resource;
      using std::experimental::pmr::null_memory_resource;
      template <typename T>
      using vector = std::vector<T, std::experimental::pmr::polymorphic_allocator<T>>;
      using string = std::basic_string<char, std::char_traits<char>,
                                       std::experimental::pmr::polymorphic_allocator<char>>;
    }
  }
#else
  #error "No memory_resource implementation available on this toolchain"
#endif

#endif // PANGOFLY_MEMORY_PMR_H_
