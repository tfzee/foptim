#pragma once
#include "helpers.hpp"
#include "stable_vec_ref.hpp"
#include "stable_vec_slot.hpp"
#include "todo.hpp"
#include "types.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <tracy/Tracy.hpp>

namespace foptim::utils {

template <class T, u32 slot_slab_len = 128, class Alloc = FAlloc<Slot<T>>>
class StableVec {
  struct FreeInfo {
    Slot<T> *ptr;
    u32 len;
  };

#ifdef SLOT_CHECK_GENERATION
  u32 curr_gen = 1;
#endif
  std::vector<FreeInfo> free_list;

public:
  static constexpr u32 _slot_slab_len = slot_slab_len;
  std::vector<Slot<T> *> _slot_slab_starts;

  StableVec() {
    _slot_slab_starts.push_back(Alloc{}.allocate(slot_slab_len));
    std::memset((void *)_slot_slab_starts.back(), 0,
                slot_slab_len * sizeof(Slot<T>));
    free_list.push_back({_slot_slab_starts.back(), slot_slab_len});
  }

  ~StableVec() {
    for (auto slot_alloc : _slot_slab_starts) {
      for (u32 i = 0; i < slot_slab_len; i++) {
        slot_alloc[i].~Slot<T>();
      }
      // TracyFree(slot_alloc);
      Alloc{}.deallocate(slot_alloc, slot_slab_len);
      // free(slot_alloc);
    }
  }

  constexpr void remove(SRef<T> s) {
    free_list.emplace_back(s.data_ref, 1);
    s.data_ref->used = SlotState::FreeList;
#ifdef SLOT_CHECK_GENERATION
    curr_gen++;
    if (curr_gen == 0) {
      curr_gen++;
    }
    s.data_ref->generation = 0;
#endif
  }

  void collect_garbage() {
    ZoneScopedN("Collect Garbage");
    curr_gen++;
    if (curr_gen == 0) {
      curr_gen++;
    }
    for (auto slot_alloc : _slot_slab_starts) {
      for (u32 i = 0; i < slot_slab_len; i++) {
        if (slot_alloc[i].used == SlotState::Free) {
          free_list.emplace_back(&slot_alloc[i], 1);
#ifdef SLOT_CHECK_GENERATION
          slot_alloc[i].generation = 0;
#endif
          slot_alloc[i].used = SlotState::FreeList;
        }
      }
    }
  }

  [[nodiscard]] constexpr size_t size_bytes() const {
    return (_slot_slab_starts.size() * slot_slab_len * sizeof(Slot<T>)) +
           (free_list.size() * sizeof(FreeInfo));
  }

  [[nodiscard]] constexpr size_t n_slabs() const {
    return _slot_slab_starts.size();
  }

  [[nodiscard]] constexpr size_t n_used() const {
    const auto n_slots = _slot_slab_starts.size() * slot_slab_len;
    size_t free_size = 0;
    for (const auto &free : free_list) {
      free_size += free.len;
    }
    return n_slots - free_size;
  }

  [[nodiscard]] constexpr size_t slab_size() const { return slot_slab_len; }

  SRef<T> push_back(T value = {}) {
    if (free_list.size() == 0) {
      _slot_slab_starts.push_back(Alloc{}.allocate(slot_slab_len));
      std::memset((void *)_slot_slab_starts.back(), 0,
                  slot_slab_len * sizeof(Slot<T>));
      // _slot_slab_starts.push_back(
      //     (Slot<T> *)calloc(slot_slab_len, sizeof(Slot<T>)));
      // TracyAlloc(_slot_slab_starts.back(), slot_slab_len * sizeof(Slot<T>));
      free_list.push_back({_slot_slab_starts.back(), slot_slab_len});
    }

    FreeInfo &target = free_list.back();
    Slot<T> *res_ptr = target.ptr;
    res_ptr->data = value;
    target.ptr++;
    target.len--;
    if (target.len == 0) {
      free_list.pop_back();
      // TODO("delete last from free list");
    }
    res_ptr->used = SlotState::Used;
#ifdef SLOT_CHECK_GENERATION
    res_ptr->generation = curr_gen;
    return SRef{res_ptr, curr_gen};
#else
    return SRef{res_ptr, 0};
#endif
  }

  void clear() { TODO("not implemented clear yet"); }
};

// template <class T, u32 slot_slab_len = 128,
//           class Alloc = IRAlloc<Slot<T>>>
// using IRStableVec = StableVec<T, slot_slab_len, Alloc>;

template <class T, u32 slot_slab_len = 128, class Alloc = FAlloc<Slot<T>>>
using FStableVec = StableVec<T, slot_slab_len, Alloc>;

} // namespace foptim::utils
