#pragma once
#include "../func.hpp"
#include "config/compiler_config.hpp"
#include "mir/optim/function_pass.hpp"

namespace foptim::fmir {

class LVN: public FunctionPass {
  u64 unique_reg_id = 0;
  // TVec<ArgData> helper1;
  // TVec<ArgData> helper2;

  MArgument get_reg(Type type);
  bool equal_enough(const MInstr &in1, const MInstr &in2);
  void apply_impl(MBB &bb);

 public:
  void apply(MFunc &funcs, const conf::CompConf&) final override;
};

}  // namespace foptim::fmir
