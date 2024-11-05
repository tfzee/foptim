#pragma once
#include <Tracy/tracy/Tracy.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

struct Region {
  Region *next;
  size_t count;
  size_t capacity;
  uintptr_t data[];
};

struct Arena {
  Region *begin, *end;
};

void *arena_alloc(Arena *a, size_t size_bytes);
void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz);
char *arena_strdup(Arena *a, const char *cstr);
void *arena_memdup(Arena *a, void *data, size_t size);
void arena_reset(Arena *a);
void arena_free(Arena *a);

extern Arena global_temp_arena;
extern Arena ir_arena;

namespace foptim::utils {

template <class T> class TempAlloc : public std::allocator<T> {
public:
  T *allocate(size_t count) {
    return (T *)arena_alloc(&global_temp_arena, count * sizeof(T));
  }

  constexpr void deallocate(T * /*unused*/,
                                                     size_t /*unused*/) {}

  static void reset() {
    TracyMessageL("ResetingTempArena");
    arena_reset(&global_temp_arena);
  }
  static void free() { arena_free(&global_temp_arena); }
};

template <class T> class IRAlloc : public std::allocator<T> {
public:
  T *allocate(size_t count) {
    return (T *)arena_alloc(&ir_arena, count * sizeof(T));
  }

  constexpr void deallocate(T * /*unused*/,
                                                     size_t /*unused*/) {}

  static void reset() { arena_reset(&ir_arena); }
  static void free() { arena_free(&ir_arena); }
};

} // namespace foptim::utils
