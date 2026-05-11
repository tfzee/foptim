#pragma once
#include "mir/instr.hpp"
#include "utils/types.hpp"
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

  IRVec<StackSlot> extra_stack_slots;

  bool void_ret = true;
  Type res_ty = Type::INVALID;

  StackSlotId get_stack_slot(u64 size) {
    extra_stack_slots.push_back(StackSlot{size});
    return extra_stack_slots.size();
  }
};

} // namespace foptim::fmir
