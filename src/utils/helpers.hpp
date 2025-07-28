#pragma once
#include <memory>
#include <tracy/Tracy.hpp>

#include "utils/types.hpp"

namespace foptim::utils {

template <class T>
class TrackingAlloc : public std::allocator<T> {
 public:
  using pointer = T *;

  constexpr T *allocate(size_t count) {
    auto ptr = std::allocator<T>::allocate(count);
    TracyAllocS(ptr, count * sizeof(T), 16);
    return ptr;
  }

  constexpr void deallocate(T *ptr, size_t count) {
    TracyFree(ptr);
    std::allocator<T>::deallocate(ptr, count);
  }
};

#if TRACY_ENABLE
template <class T>
using FAlloc = TrackingAlloc<T>;
#else
template <class T>
using FAlloc = std::allocator<T>;
#endif

__attribute__((const)) static inline bool is_pow2(u128 x) {
  return (x != 0) && ((x & (x - 1)) == 0);
}
// returns the number of bits needed to store this value
// so (1 << npow2) will give you the actual next bigger or requal power of 2
__attribute__((const)) static inline u128 npow2(u128 x) {
  if (x < 2) {
    return 0;
  }
  return (128 - __builtin_clzg(x - 1));
}

}  // namespace foptim::utils
