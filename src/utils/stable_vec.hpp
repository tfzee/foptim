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
#include "utils/mutex.hpp"
#include "utils/vec.hpp"

namespace foptim::utils {

template <class T>
struct FreeInfo {
  Slot<T> *ptr;
  u32 len;
};

template <
    class T, u32 slot_slab_len = 128, class AllocFreeList = FAlloc<FreeInfo<T>>,
    class AllocSlabList = FAlloc<Slot<T> *>, class AllocSlabs = FAlloc<Slot<T>>>
class StableVec {
#ifdef SLOT_CHECK_GENERATION
  std::atomic<u32> curr_gen = 1;
#endif
  foptim::Mutex<std::vector<FreeInfo<T>, AllocFreeList>> _free_list;

 public:
  static constexpr u32 _slot_slab_len = slot_slab_len;
  foptim::Mutex<std::vector<Slot<T> *, AllocSlabList>> _slot_slab_starts;

  StableVec() {
    Slot<T> *new_slab = AllocSlabs{}.allocate(slot_slab_len);
    std::memset((void *)new_slab, 0, slot_slab_len * sizeof(Slot<T>));
    {
      auto slot_slab_starts = _slot_slab_starts.scoped_lock();
      slot_slab_starts->push_back(new_slab);
    }
    {
      auto free_list = _free_list.scoped_lock();
      free_list->push_back({new_slab, slot_slab_len});
    }
  }

  ~StableVec() {
    {
      auto free_list = _free_list.scoped_lock();
      free_list->clear();
    }
    auto slot_slab_starts = _slot_slab_starts.scoped_lock();
    for (auto slot_alloc : *slot_slab_starts) {
      for (u32 i = 0; i < slot_slab_len; i++) {
        slot_alloc[i].~Slot<T>();
      }
      // TracyFree(slot_alloc);
      AllocSlabs{}.deallocate(slot_alloc, slot_slab_len);
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
    TVec<FreeInfo<T>> temp_info;
    {
#ifdef SLOT_CHECK_GENERATION
      curr_gen.fetch_add(1);
      if (curr_gen.load() == 0) {
        curr_gen.fetch_add(1);
      }
#endif
      {
        auto slot_slab_starts = _slot_slab_starts.scoped_lock();
        for (auto slot_alloc : *slot_slab_starts) {
          for (u32 i = 0; i < slot_slab_len; i++) {
            auto exp = SlotState::Free;
            if (std::atomic_compare_exchange_strong(&slot_alloc[i].used, &exp,
                                                    SlotState::FreeList)) {
              temp_info.emplace_back(&slot_alloc[i], 1);
#ifdef SLOT_CHECK_GENERATION
              slot_alloc[i].generation.store(0);
#endif
            }
          }
        }
      }
    }
    {
      auto free_list = _free_list.scoped_lock();
      free_list->insert(free_list->end(), temp_info.begin(), temp_info.end());
    }
  }

  [[nodiscard]] constexpr size_t size_bytes() const {
    return (_slot_slab_starts.scoped_lock()->size() * slot_slab_len *
            sizeof(Slot<T>)) +
           (_free_list.scoped_lock()->size() * sizeof(FreeInfo<T>));
  }

  [[nodiscard]] constexpr size_t n_slabs() const {
    return _slot_slab_starts.scoped_lock()->size();
  }

  [[nodiscard]] constexpr size_t n_used() const {
    const auto n_slots =
        _slot_slab_starts.scoped_lock()->size() * slot_slab_len;
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
          auto slot_slab_starts = _slot_slab_starts.scoped_lock();
          slot_slab_starts->push_back(AllocSlabs{}.allocate(slot_slab_len));
          target.ptr = slot_slab_starts->back();
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
      res_ptr->generation = curr_gen.load(std::memory_order::acquire);
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
