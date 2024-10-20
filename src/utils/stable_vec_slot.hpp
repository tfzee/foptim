#pragma once
#include "types.hpp"

namespace foptim::utils {
template <class T> struct Slot {
  u32 generation;
  bool used = false;
  T data;
};

} // namespace foptim::utils
