#pragma once
#include "utils/vec.hpp"

namespace foptim::fmir {
class MFunc;

class RegisterJoining {
  void apply(MFunc &func);

 public:
  void apply(foptim::FVec<MFunc> &funcs);
};
}  // namespace foptim::fmir
