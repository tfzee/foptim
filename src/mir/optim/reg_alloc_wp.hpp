#pragma once
#include "../func.hpp"

namespace foptim::fmir {

// Reg Alloc with the Welsh Powell (WP) graph coloring Algorithm
// first constructing the intereference and then doing WP
class RegAllocWP {
 public:
  void apply(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
