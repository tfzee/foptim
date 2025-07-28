#pragma once
#include "../func.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class BBReordering {
 public:
  void apply(MFunc &func);
  void apply(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
