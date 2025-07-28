#pragma once
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
  u32 generation;
#endif
  SlotState used;
  T data;
};

}  // namespace foptim::utils
