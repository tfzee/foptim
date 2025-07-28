#pragma once
#include "../func.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class CallingConv {
 public:
  // run before final  register alloc
  // sets up argument loading
  void first_stage(FVec<MFunc> &funcs);

  // run after final register alloc
  // lowers the invokes to calls and sets up their arguments
  void second_stage(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
