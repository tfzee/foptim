#pragma once
#include <atomic>

#include "types.hpp"

// How much SRef checks on every dereference (0: none, 1: slot in use,
// 2: additionally the generation), set by CMake (slot_check_level)
#ifndef SLOT_CHECK_LEVEL
#ifdef SLOT_CHECK_GENERATION
#define SLOT_CHECK_LEVEL 2
#else
#define SLOT_CHECK_LEVEL 1
#endif
#endif

namespace foptim::utils {

enum class SlotState : u8 {
  FreeList = 0,
  Used = 1,
  Free = 2,
};

// State and generation of a slot live in one word, so a ref can be validated
// with a single load + compare and both are published/invalidated together.
//   word = (generation << 2) | state
// All zero (freshly memset slab) is a FreeList slot with generation 0. The
// generation is only stored when SLOT_CHECK_LEVEL >= 2, otherwise it is 0.
inline constexpr u64 SLOT_STATE_MASK = 3;

constexpr u64 slot_word(u32 generation, SlotState state) {
#if SLOT_CHECK_LEVEL >= 2
  return (static_cast<u64>(generation) << 2) | static_cast<u64>(state);
#else
  (void)generation;
  return static_cast<u64>(state);
#endif
}

template <class T>
struct Slot {
  std::atomic<u64> word;
  static_assert(std::atomic<u64>::is_always_lock_free);
  T data;

  [[nodiscard]] SlotState state(
      std::memory_order order = std::memory_order_acquire) const {
    return static_cast<SlotState>(word.load(order) & SLOT_STATE_MASK);
  }
  [[nodiscard]] bool is_used(
      std::memory_order order = std::memory_order_acquire) const {
    return state(order) == SlotState::Used;
  }
  // always 0 if generations are not tracked
  [[nodiscard]] u32 generation(
      std::memory_order order = std::memory_order_acquire) const {
#if SLOT_CHECK_LEVEL >= 2
    return static_cast<u32>(word.load(order) >> 2);
#else
    (void)order;
    return 0;
#endif
  }
  // make the slot live with the given generation
  void publish(u32 generation) {
    word.store(slot_word(generation, SlotState::Used),
               std::memory_order_release);
  }
  // erased but not yet collected, a ref to it keeps its generation but fails
  // the Used check
  void mark_free() {
    word.store(slot_word(generation(), SlotState::Free),
               std::memory_order_release);
  }
  // reusable, invalidates every ref to it
  void mark_free_list() { word.store(0, std::memory_order_release); }
  // Free -> FreeList, false if the slot was not Free
  bool try_collect() {
    u64 w = word.load(std::memory_order_relaxed);
    if ((w & SLOT_STATE_MASK) != static_cast<u64>(SlotState::Free)) {
      return false;
    }
    return word.compare_exchange_strong(w, 0);
  }
};

}  // namespace foptim::utils
