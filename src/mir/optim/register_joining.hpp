#pragma once
#include "utils/vec.hpp"

namespace foptim::fmir {
class MFunc;

class RegisterJoining {
  void apply_impl(MFunc &func);

 public:
  void apply(MFunc &func);
  void apply(foptim::FVec<MFunc> &funcs);
};
}  // namespace foptim::fmir
