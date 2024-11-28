#pragma once
#include "../func.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class ArgLowering {
public:
  void apply(FVec<MFunc> &funcs);
};

} // namespace foptim::fmir

