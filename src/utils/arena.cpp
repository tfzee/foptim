// Copyright 2022 Alexey Kutepov <reximkut@gmail.com>

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "arena.hpp"
#include <tracy/Tracy.hpp>


#include <cstring>

#ifndef ARENA_ASSERT
#include <cassert>
#define ARENA_ASSERT assert
#endif

#define ARENA_BACKEND_LIBC_MALLOC 0
#define ARENA_BACKEND_LINUX_MMAP 1

#ifndef ARENA_BACKEND
#define ARENA_BACKEND ARENA_BACKEND_LIBC_MALLOC
#endif // ARENA_BACKEND

#define REGION_DEFAULT_CAPACITY (size_t)(8 * 1024)

Region *new_region(size_t capacity);
void free_region(Region *r);

#define ARENA_DA_INIT_CAP 256


Arena global_temp_arena = {nullptr, nullptr};
Arena ir_arena = {nullptr, nullptr};



#ifdef __cplusplus
#define cast_ptr(ptr) (decltype(ptr))
#else
#define cast_ptr(...)
#endif

#define arena_da_append(a, da, item)                                           \
  do {                                                                         \
    if ((da)->count >= (da)->capacity) {                                       \
      size_t new_capacity =                                                    \
          (da)->capacity == 0 ? ARENA_DA_INIT_CAP : (da)->capacity * 2;        \
      (da)->items = cast_ptr((da)->items) arena_realloc(                       \
          (a), (da)->items, (da)->capacity * sizeof(*(da)->items),             \
          new_capacity * sizeof(*(da)->items));                                \
      (da)->capacity = new_capacity;                                           \
    }                                                                          \
                                                                               \
    (da)->items[(da)->count++] = (item);                                       \
  } while (0)

#if ARENA_BACKEND == ARENA_BACKEND_LIBC_MALLOC
#include <cstdlib>

// TODO: instead of accepting specific capacity new_region() should accept the
// size of the object we want to fit into the region It should be up to
// new_region() to decide the actual capacity to allocate
Region *new_region(size_t capacity) {
  size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
  // TODO: it would be nice if we could guarantee that the regions are allocated
  // by ARENA_BACKEND_LIBC_MALLOC are page aligned
  TracyMessageL("TempArenaRegionAlloc");
  Region *r = (Region *)malloc(size_bytes);
  TracyAlloc(r, size_bytes);
  ARENA_ASSERT(r);
  r->next = nullptr;
  r->count = 0;
  r->capacity = capacity;
  return r;
}

void free_region(Region *r) {
  TracyFree(r);
  free(r);
}
#elif ARENA_BACKEND == ARENA_BACKEND_LINUX_MMAP
#include <sys/mman.h>
#include <unistd.h>

Region *new_region(size_t capacity) {
  size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
  Region *r = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  ARENA_ASSERT(r != MAP_FAILED);
  r->next = NULL;
  r->count = 0;
  r->capacity = capacity;
  return r;
}

void free_region(Region *r) {
  size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * r->capacity;
  int ret = munmap(r, size_bytes);
  ARENA_ASSERT(ret == 0);
}

#else
#error "Unknown Arena backend"
#endif

// TODO: add debug statistic collection mode for arena
// Should collect things like:
// - How many times new_region was called
// - How many times existing region was skipped
// - How many times allocation exceeded REGION_DEFAULT_CAPACITY

void *arena_alloc(Arena *a, size_t size_bytes) {
  size_t size = (size_bytes + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);

  if (a->end == nullptr) {
    ARENA_ASSERT(a->begin == nullptr);
    size_t capacity = REGION_DEFAULT_CAPACITY;
    if (capacity < size) {
      capacity = size;
    }
    a->end = new_region(capacity);
    a->begin = a->end;
  }

  while (a->end->count + size > a->end->capacity && a->end->next != nullptr) {
    a->end = a->end->next;
  }

  if (a->end->count + size > a->end->capacity) {
    ARENA_ASSERT(a->end->next == nullptr);
    size_t capacity = REGION_DEFAULT_CAPACITY;
    if (capacity < size) {
      capacity = size;
    }
    a->end->next = new_region(capacity);
    a->end = a->end->next;
  }

  void *result = &a->end->data[a->end->count];
  a->end->count += size;
  return result;
}

void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz) {
  if (newsz <= oldsz) {
    return oldptr;
  }
  void *newptr = arena_alloc(a, newsz);
  char *newptr_char = (char *)newptr;
  char *oldptr_char = (char *)oldptr;
  for (size_t i = 0; i < oldsz; ++i) {
    newptr_char[i] = oldptr_char[i];
  }
  return newptr;
}

char *arena_strdup(Arena *a, const char *cstr) {
  size_t n = strlen(cstr);
  char *dup = (char *)arena_alloc(a, n + 1);
  memcpy(dup, cstr, n);
  dup[n] = '\0';
  return dup;
}

void *arena_memdup(Arena *a, void *data, size_t size) {
  return memcpy(arena_alloc(a, size), data, size);
}

void arena_reset(Arena *a) {
  for (Region *r = a->begin; r != nullptr; r = r->next) {
    r->count = 0;
  }
  a->end = a->begin;
}

void arena_free(Arena *a) {
  Region *r = a->begin;
  while (r != nullptr) {
    Region *r0 = r;
    r = r->next;
    free_region(r0);
  }
  a->begin = nullptr;
  a->end = nullptr;
}
