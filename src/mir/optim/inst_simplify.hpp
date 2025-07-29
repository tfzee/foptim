#pragma once
#include "../func.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class InstSimplify {
 public:
  void apply(MFunc &funcs);
  void apply(FVec<MFunc> &funcs);
  void early_apply(MFunc &funcs);
  void early_apply(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
