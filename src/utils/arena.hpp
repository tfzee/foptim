#pragma once
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

namespace foptim::utils {

template <class T> class TempAlloc : public std::allocator<T> {
public:
  T *allocate(size_t count) { return arena_alloc(&global_temp_arena, count); }

  [[clang::always_inline]] constexpr void deallocate() {}

  static void reset() { arena_reset(&global_temp_arena); }
  static void clear() { arena_reset(&global_temp_arena); }
};

} // namespace foptim::utils
