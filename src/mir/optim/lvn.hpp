#pragma once
#include "../func.hpp"

namespace foptim::fmir {

class LVN {
  u64 unique_reg_id = 0;
  // TVec<ArgData> helper1;
  // TVec<ArgData> helper2;

  MArgument get_reg(Type type);
  bool equal_enough(const MInstr &in1, const MInstr &in2);
  void apply_impl(MBB &bb);

 public:
  void apply(MFunc &funcs);
};

}  // namespace foptim::fmir
