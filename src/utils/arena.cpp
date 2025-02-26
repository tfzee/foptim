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
#include <cstdint>
#include <tracy/Tracy.hpp>

#include <cstring>

#ifndef ARENA_ASSERT
#include <cassert>
#define ARENA_ASSERT assert
#endif

#define REGION_DEFAULT_CAPACITY (size_t)(8 * 1024)

Region *new_region(size_t capacity);
void free_region(Region *r);

#define ARENA_DA_INIT_CAP 256

Arena global_temp_arena = {nullptr, nullptr};
Arena ir_arena = {nullptr, nullptr};
unsigned long temp_arena_size = 0;
unsigned long temp_ir_size = 0;

#define cast_ptr(ptr) (decltype(ptr))
#include <cstdlib>

Region *new_region(size_t capacity) {
  size_t size_bytes = sizeof(Region) + sizeof(uintptr_t) * capacity;
  Region *r = (Region *)malloc(size_bytes);
  // assert((size_t)r % alignof(std::max_align_t) == 0);
  // assert((size_t)r % sizeof(uintptr_t) == 0);
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

void *arena_alloc(Arena *a, size_t size_bytes) {
  size_t aligned_size = ((size_bytes + (MIN_CHUNK_SIZE - 1)) & -MIN_CHUNK_SIZE);
  // assert(aligned_size % alignof(std::max_align_t) == 0);
  // assert(aligned_size % sizeof(uintptr_t) == 0);

  size_t size = (aligned_size) / sizeof(uintptr_t);
  // printf("%zu Aligned size %zu %zu\n", size_bytes, aligned_size, size);

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
