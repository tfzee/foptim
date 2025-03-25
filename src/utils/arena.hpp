#pragma once
#include "utils/types.hpp"
#include <client/TracyLock.hpp>
#include <common/TracySystem.hpp>
#include <cstddef>
#include <fmt/core.h>
#include <mutex>
#include <tracy/Tracy.hpp>

struct Arena;

void *arena_alloc(Arena *a, size_t size_bytes);
// void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz);
// char *arena_strdup(Arena *a, const char *cstr);
// void *arena_memdup(Arena *a, void *data, size_t size);
void arena_reset(Arena *a);
void arena_free(Arena *a);

extern thread_local Arena temp_arena;
extern thread_local unsigned long temp_arena_size;

extern Arena ir_arena;
extern tracy::Lockable<std::mutex> ir_arena_mutex;
extern std::atomic<unsigned long> temp_ir_size;
// static thread_local const char* alloc_name = "TempAlloc";

namespace foptim::utils {

template <class T> class TempAlloc {
public:
  using value_type = T;
  using pointer = T *;
  consteval TempAlloc() noexcept = default;
  template <class U> constexpr TempAlloc(const TempAlloc<U> &) noexcept {}

  T *allocate(size_t count) {
    auto ptr = (T *)arena_alloc(&temp_arena, count * sizeof(T));
    temp_arena_size += count * sizeof(T);
    // static thread_local const auto alloc_name =
    //     std::string("TempAlloc:") + thread_name;
    TracyPlot(thread_name, (foptim::i64)temp_arena_size);
    return ptr;
  }

  constexpr void deallocate(T * /*unused*/, size_t /*unused*/) {}

  static void reset() {
    temp_arena_size = 0;
    // static thread_local const auto alloc_name =
    //     std::string("TempAlloc:") + thread_name;
    TracyPlot(thread_name, (foptim::i64)temp_arena_size);
    arena_reset(&temp_arena);
  }
  static void free() {
    arena_free(&temp_arena);
    // TODO: tracy free pool
  }
  template <typename U> struct rebind {
    using other = TempAlloc<U>;
  };
};

template <class T> class IRAlloc {
public:
  using value_type = T;
  using pointer = T *;
  consteval IRAlloc() noexcept = default;
  template <class U> constexpr IRAlloc(const IRAlloc<U> &) noexcept {}

  T *allocate(size_t count) {
    T *ptr = nullptr;
    {
      std::lock_guard<tracy::Lockable<std::mutex>> arena_guard{ir_arena_mutex};
      ptr = (T *)arena_alloc(&ir_arena, count * sizeof(T));
    }
    temp_ir_size += count * sizeof(T);
    TracyAllocNS(ptr, sizeof(T) * count, 10, "IRAlloc");
    return ptr;
  }

  constexpr void deallocate(T * /*unused*/, size_t /*unused*/) {}

  static void reset() {
    std::lock_guard<tracy::Lockable<std::mutex>> arena_guard{ir_arena_mutex};
    arena_reset(&ir_arena);
    temp_ir_size = 0;
    // TODO: tracy free pool
  }
  static void free() {
    std::lock_guard<tracy::Lockable<std::mutex>> arena_guard{ir_arena_mutex};
    arena_free(&ir_arena);
  }

  template <typename U> struct rebind {
    using other = IRAlloc<U>;
  };
};

template <class T, class U>
constexpr bool operator==(const IRAlloc<T> &, const IRAlloc<U> &) noexcept {
  return true;
}
template <class T, class U>
constexpr bool operator!=(const IRAlloc<T> &, const IRAlloc<U> &) noexcept {
  return false;
}

template <class T, class U>
constexpr bool operator==(const TempAlloc<T> &, const TempAlloc<U> &) noexcept {
  return true;
}

template <class T, class U>
constexpr bool operator!=(const TempAlloc<T> &, const TempAlloc<U> &) noexcept {
  return false;
}
} // namespace foptim::utils
