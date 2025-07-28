#pragma once
#include "../func.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class DeadCodeElim {
  void apply(MFunc &func);

 public:
  void apply(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
