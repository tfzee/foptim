#pragma once
#include <atomic>

#include "types.hpp"

namespace foptim::utils {

enum class SlotState : u8 {
  FreeList = 0,
  Used = 1,
  Free = 2,
};

template <class T>
struct Slot {
#ifdef SLOT_CHECK_GENERATION
  std::atomic<u32> generation;
#endif
  std::atomic<SlotState> used;
  static_assert(std::atomic<SlotState>::is_always_lock_free);
  T data;
};

}  // namespace foptim::utils
