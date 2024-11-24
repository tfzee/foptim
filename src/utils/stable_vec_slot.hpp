#pragma once
#include "types.hpp"

namespace foptim::utils {
template <class T> struct Slot {
#ifdef SLOT_CHECK_GENERATION
  u32 generation;
#endif
  bool used = false;
  T data;
};

} // namespace foptim::utils
