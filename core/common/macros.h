#ifndef PANGOFLY_COMMON_MACROS_H_
#define PANGOFLY_COMMON_MACROS_H_

#define PANGOFLY_DECLARE_HANDLE(HandleName) \
  typedef struct HandleName##__ { int unused; } *HandleName

#define PANGOFLY_DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;               \
  TypeName& operator=(const TypeName&) = delete

#define PANGOFLY_CONSTEXPR constexpr

#endif // PANGOFLY_COMMON_MACROS_H_