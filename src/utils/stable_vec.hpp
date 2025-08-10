#pragma once
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <tracy/Tracy.hpp>
#include <vector>

#include "helpers.hpp"
#include "stable_vec_ref.hpp"
#include "stable_vec_slot.hpp"
#include "todo.hpp"
#include "types.hpp"
#include "utils/arena.hpp"
#include "utils/mutex.hpp"
#include "utils/vec.hpp"

namespace foptim::utils {

template <class T>
struct FreeInfo {
  Slot<T> *ptr;
  u32 len;
};

template <class T, u32 slot_slab_len = 128>
struct Slab {
  Slab *next = nullptr;
  Slot<T> data[slot_slab_len];
};

template <class T, u32 slot_slab_len = 128,
          class AllocFreeList = FAlloc<FreeInfo<T>>,
          class AllocSlabs = FAlloc<Slab<T, slot_slab_len>>>
class StableVec {
#ifdef SLOT_CHECK_GENERATION
  std::atomic<u32> curr_gen = 1;
#endif
  foptim::Mutex<std::vector<FreeInfo<T>, AllocFreeList>> _free_list;

 public:
  using Slab = Slab<T, slot_slab_len>;
  static constexpr u32 _slot_slab_len = slot_slab_len;
  std::atomic<Slab *> _slot_start;

  void slap_append(Slab *new_slab) {
    new_slab->next = _slot_start.load();
    while (!_slot_start.compare_exchange_strong(new_slab->next, new_slab)) {
    }
  }

  [[nodiscard]] u32 nslabs() const {
    auto *start = _slot_start.load();
    u32 n = 0;
    while (start != nullptr) {
      start = start->next;
      n++;
    }
    return n;
  }

  StableVec() {
    Slab *new_slab = AllocSlabs{}.allocate(1);
    std::memset((void *)&new_slab->data[0], 0, slot_slab_len * sizeof(Slot<T>));
    {
      slap_append(new_slab);
    }
    {
      auto free_list = _free_list.scoped_lock();
      free_list->push_back({&new_slab->data[0], slot_slab_len});
    }
  }

  ~StableVec() {
    {
      auto free_list = _free_list.scoped_lock();
      free_list->clear();
    }
    Slab *start = _slot_start;
    while (start != nullptr) {
      for (u32 i = 0; i < slot_slab_len; i++) {
        start->data[i].~Slot<T>();
      }
      // TracyFree(slot_alloc);
      auto next = start->next;
      AllocSlabs{}.deallocate(start, 1);
      start = next;
      // free(slot_alloc);
    }
  }

  constexpr void remove(SRef<T> s) {
    s.data_ref->used = SlotState::FreeList;
    {
#ifdef SLOT_CHECK_GENERATION
      s.data_ref->generation = 0;
      curr_gen++;
      if (curr_gen == 0) {
        curr_gen++;
      }
#endif
      auto free_list = _free_list.scoped_lock();
      free_list->emplace_back(s.data_ref, 1);
    }
  }

  void collect_garbage() {
    ZoneScopedN("Collect Garbage");
    auto save_point = TempAlloc<void *>::save();
    TVec<FreeInfo<T>> temp_info;
    {
#ifdef SLOT_CHECK_GENERATION
      curr_gen.fetch_add(1);
      if (curr_gen.load() == 0) {
        curr_gen.fetch_add(1);
      }
#endif
      Slab *slot_slab_start = _slot_start.load();
      while (slot_slab_start) {
        for (u32 i = 0; i < slot_slab_len; i++) {
          auto exp = SlotState::Free;
          if (std::atomic_compare_exchange_strong(
                  &slot_slab_start->data[i].used, &exp, SlotState::FreeList)) {
            temp_info.emplace_back(&slot_slab_start->data[i], 1);
#ifdef SLOT_CHECK_GENERATION
            slot_slab_start->data[i].generation.store(0);
#endif
          }
        }
        slot_slab_start = slot_slab_start->next;
      }
    }
    {
      auto free_list = _free_list.scoped_lock();
      free_list->insert(free_list->end(), temp_info.begin(), temp_info.end());
    }
    TempAlloc<void *>::restore(save_point);
  }

  [[nodiscard]] constexpr size_t size_bytes() const {
    return (nslabs() * slot_slab_len * sizeof(Slot<T>)) +
           (_free_list.scoped_lock()->size() * sizeof(FreeInfo<T>));
  }

  [[nodiscard]] constexpr size_t n_used() const {
    const auto n_slots = nslabs() * slot_slab_len;
    size_t free_size = 0;
    {
      auto free_list = _free_list.scoped_lock();
      for (const auto &free : *free_list.operator->()) {
        free_size += free.len;
      }
    }
    return n_slots - free_size;
  }

  [[nodiscard]] constexpr size_t slab_size() const { return slot_slab_len; }

  SRef<T> push_back(T value = {}) {
    Slot<T> *res_ptr = nullptr;
    {
      FreeInfo<T> target{nullptr, 0};
      bool is_new = false;
      {
        auto free_list = _free_list.scoped_lock();
        if (free_list->size() != 0) {
          target = free_list->back();
          free_list->pop_back();
        } else {
          auto slab = AllocSlabs{}.allocate(1);
          slap_append(slab);
          target.ptr = &slab->data[0];
          target.len = slot_slab_len;
          is_new = true;
        }
      }
      if (is_new) {
        std::memset((void *)target.ptr, 0, target.len * sizeof(Slot<T>));
      }
      res_ptr = target.ptr;
      res_ptr->data = std::move(value);
      target.ptr++;
      target.len--;
      if (target.len != 0) {
        auto free_list = _free_list.scoped_lock();
        free_list->push_back(target);
      }
#ifdef SLOT_CHECK_GENERATION
      res_ptr->generation = curr_gen.load(std::memory_order::acquire);
#endif
      res_ptr->used.store(SlotState::Used, std::memory_order::release);
    }
#ifdef SLOT_CHECK_GENERATION
    return SRef{res_ptr, res_ptr->generation};
#else
    return SRef{res_ptr, 0};
#endif
  }

  void clear() { TODO("not implemented clear yet"); }
};

// template <class T, u32 slot_slab_len = 128,
//           class Alloc = IRAlloc<Slot<T>>>
// using IRStableVec = StableVec<T, slot_slab_len, Alloc>;

}  // namespace foptim::utils
