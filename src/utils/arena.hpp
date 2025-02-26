#pragma once
#include "utils/types.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tracy/Tracy.hpp>

constexpr unsigned int MIN_CHUNK_SIZE = alignof(std::max_align_t);

struct Region {
  Region *next;
  size_t count;
  size_t capacity;
  alignas(MIN_CHUNK_SIZE) uintptr_t data[];
};

struct Arena {
  Region *begin, *end;
};

void *arena_alloc(Arena *a, size_t size_bytes);
// void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz);
// char *arena_strdup(Arena *a, const char *cstr);
// void *arena_memdup(Arena *a, void *data, size_t size);
void arena_reset(Arena *a);
void arena_free(Arena *a);

extern Arena global_temp_arena;
extern Arena ir_arena;
extern unsigned long temp_arena_size;
extern unsigned long temp_ir_size;

namespace foptim::utils {

template <class T> class TempAlloc : public std::allocator<T> {
public:
  using pointer = T *;

  T *allocate(size_t count) {
    auto ptr = (T *)arena_alloc(&global_temp_arena, count * sizeof(T));
    temp_arena_size += count * sizeof(T);
    TracyPlot("TempAlloc", (foptim::i64)temp_arena_size);
    // TracyAllocN(ptr, sizeof(T) * count, "TAlloc");
    return ptr;
  }

  constexpr void deallocate(T * /*unused*/, size_t /*unused*/) {}

  static void reset() {
    temp_arena_size = 0;
    TracyPlot("TempAlloc", (foptim::i64)temp_arena_size);
    arena_reset(&global_temp_arena);
  }
  static void free() {
    arena_free(&global_temp_arena);
    // TODO: tracy free pool
  }
};

template <class T> class IRAlloc : public std::allocator<T> {
public:
  using pointer = T *;

  T *allocate(size_t count) {
    auto ptr = (T *)arena_alloc(&ir_arena, count * sizeof(T));
    temp_ir_size += count * sizeof(T);
    TracyPlot("IRAlloc", (foptim::i64)temp_ir_size);
    TracyAllocNS(ptr, sizeof(T) * count, 10, "IRAlloc");
    return ptr;
  }

  constexpr void deallocate(T * /*unused*/, size_t /*unused*/) {}

  static void reset() {
    arena_reset(&ir_arena);
    temp_ir_size = 0;
    TracyPlot("IRAlloc", (foptim::i64)temp_ir_size);
    // TODO: tracy free pool
  }
  static void free() { arena_free(&ir_arena); }
};

} // namespace foptim::utils
