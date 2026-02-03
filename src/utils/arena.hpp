#pragma once
#include <fmt/core.h>

#include <atomic>
#include <cstddef>

#include "utils/tracy.hpp"
#include "utils/types.hpp"

struct Region;

using Arena = struct {
  Region *begin, *end;
};

using Arena_Mark = struct {
  Region *region;
  size_t count;
};

void *arena_alloc(Arena *a, size_t size_bytes);
// void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz);
// char *arena_strdup(Arena *a, const char *cstr);
// void *arena_memdup(Arena *a, void *data, size_t size);
Arena_Mark arena_snapshot(Arena *a);
void arena_rewind(Arena *a, Arena_Mark m);
void arena_reset(Arena *a);
void arena_free(Arena *a);

extern thread_local Arena temp_arena;
extern thread_local unsigned long temp_arena_size;

extern thread_local Arena ir_arena;
extern thread_local unsigned long ir_arena_size;
// static thread_local const char* alloc_name = "TempAlloc";

namespace foptim::utils {

template <class T>
class TempAlloc {
 public:
  using value_type = T;
  using pointer = T *;
  consteval TempAlloc() noexcept = default;
  template <class U>
  constexpr TempAlloc(const TempAlloc<U> & /*unnused*/) noexcept {}

  class ScopedTempStorage {
   public:
    Arena_Mark _stored;
    ScopedTempStorage(Arena_Mark m) : _stored(m) {}
    ~ScopedTempStorage() { TempAlloc::restore(_stored); }
  };

  static ScopedTempStorage scoped() { return {save()}; }

  T *allocate(size_t count) {
    auto ptr = (T *)arena_alloc(&temp_arena, count * sizeof(T));
    temp_arena_size += count * sizeof(T);
    // TracyMessageLS("temp_alloc", 10);
    TracyPlot(thread_name,
              (foptim::i64)ir_arena_size + (foptim::i64)temp_arena_size);
    return ptr;
  }

  constexpr void deallocate(T * /*unused*/, size_t /*unused*/) {}

  static Arena_Mark save() { return arena_snapshot(&temp_arena); }

  static void restore(Arena_Mark mark) { arena_rewind(&temp_arena, mark); }

  static void reset() {
    temp_arena_size = 0;
    // static thread_local const auto alloc_name =
    //     std::string("TempAlloc:") + thread_name;
    TracyPlot(thread_name,
              (foptim::i64)ir_arena_size + (foptim::i64)temp_arena_size);
    arena_reset(&temp_arena);
  }
  static void free() {
    arena_free(&temp_arena);
    // TODO: tracy free pool
  }
  template <typename U>
  struct rebind {
    using other = TempAlloc<U>;
  };
};

template <class T>
class IRAlloc {
 public:
  using value_type = T;
  using pointer = T *;
  consteval IRAlloc() noexcept = default;
  template <class U>
  constexpr IRAlloc(const IRAlloc<U> & /*unnused*/) noexcept {}

  T *allocate(size_t count) {
    T *ptr = nullptr;
    {
      // #ifdef TRACY_ENABLE
      //       std::lock_guard<tracy::Lockable<std::mutex>>
      //       arena_guard{ir_arena_mutex};
      // #else
      //       std::lock_guard<std::mutex> arena_guard{ir_arena_mutex};
      // #endif
      ptr = (T *)arena_alloc(&ir_arena, count * sizeof(T));
    }
    ir_arena_size += count * sizeof(T);
    TracyPlot(thread_name,
              (foptim::i64)ir_arena_size + (foptim::i64)temp_arena_size);
    // TracyAllocNS(ptr, sizeof(T) * count, 10, "IRAlloc");
    return ptr;
  }

  constexpr void deallocate(T * /*unused*/, size_t /*unused*/) {}

  static void free() {
    ir_arena_size = 0;
    TracyPlot(thread_name,
              (foptim::i64)ir_arena_size + (foptim::i64)temp_arena_size);
    arena_free(&ir_arena);
  }

  template <typename U>
  struct rebind {
    using other = IRAlloc<U>;
  };
};

template <class T, class U>
constexpr bool operator==(const IRAlloc<T> & /*unnused*/,
                          const IRAlloc<U> & /*unnused*/) noexcept {
  return true;
}
template <class T, class U>
constexpr bool operator!=(const IRAlloc<T> & /*unnused*/,
                          const IRAlloc<U> & /*unnused*/) noexcept {
  return false;
}

template <class T, class U>
constexpr bool operator==(const TempAlloc<T> & /*unnused*/,
                          const TempAlloc<U> & /*unnused*/) noexcept {
  return true;
}

template <class T, class U>
constexpr bool operator!=(const TempAlloc<T> & /*unnused*/,
                          const TempAlloc<U> & /*unnused*/) noexcept {
  return false;
}
}  // namespace foptim::utils
