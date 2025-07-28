#pragma once
#include "mir/instr.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class MBB {
 public:
  IRVec<MInstr> instrs;
};

class MFunc {
 public:
  IRVec<MBB> bbs;
  IRVec<VReg> args;

  IRString name;
  bool variadic = false;
  bool needs_register_save_area = false;
  // IRVec<Type> arg_tys;

  uint32_t curr_arg_stack_off;

  bool void_ret = true;
  Type res_ty = Type::INVALID;
};

}  // namespace foptim::fmir
