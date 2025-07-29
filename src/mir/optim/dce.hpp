#pragma once
#include "../func.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class DeadCodeElim {
  void apply_impl(MFunc &func);

 public:
  void apply(MFunc &funcs);
  void apply(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
