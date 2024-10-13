#pragma once
#include "ir/attributable.hpp"
#include "mir/instr.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class MBB {
public:
  FVec<MInstr> instrs;
};

class MFunc : public fir::Attributable {
public:
  FVec<MBB> bbs;
  FVec<VReg> args;

  std::string name = "UNKNOWN";
  FVec<Type> arg_tys;

  uint32_t curr_arg_stack_off;
  
  bool void_ret = true;
  Type res_ty = Type::INVALID;
};

} // namespace foptim::fmir
