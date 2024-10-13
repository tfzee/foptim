#pragma once
#include "../func.hpp"


namespace foptim::fmir {

class RegAlloc {
public:
  void apply(FVec<MFunc> &funcs);
};

} // namespace foptim::fmir
