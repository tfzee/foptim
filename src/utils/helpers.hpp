#pragma once
#include <memory>
#include <tracy/Tracy.hpp>

namespace foptim::utils {

template <class T> class TrackingAlloc : public std::allocator<T> {
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
template <class T> using FAlloc = TrackingAlloc<T>;
#else
template <class T> using FAlloc = std::allocator<T>;
#endif

} // namespace foptim::utils
