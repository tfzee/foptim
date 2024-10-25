#pragma once
#include "helpers.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include <vector>

namespace foptim {

template <class Val, class Alloc = utils::FAlloc<Val>>
using FVec = std::vector<Val, Alloc>;

template <class Val, class Alloc = utils::TempAlloc<Val>>
using TVec = std::vector<Val, Alloc>;

template <class Val, class Alloc = utils::IRAlloc<Val>>
using IRVec = std::vector<Val, Alloc>;

template <class T, class Alloc>
utils::Printer operator<<(utils::Printer p, const std::vector<T, Alloc> data) {
  p << "[";
  for (const T &elem : data) {
    p << elem << ", ";
  }
  return p << "]";
}

} // namespace foptim
