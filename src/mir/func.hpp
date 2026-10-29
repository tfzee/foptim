#pragma once
#include "ir/inline_asm.hpp"
#include "mir/instr.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class MBB {
 public:
  IRVec<MInstr> instrs;
};

// one inline asm statement of a function, copied from the module table
struct MAsmBlock {
  IRString tmpl;
  IRVec<fir::AsmOperand> operands;
  bool sideeffect = false;
  bool mem_clobber = false;
};

class MFunc {
 public:
  IRVec<MBB> bbs;
  IRVec<VReg> args;

  IRString name;
  fir::Linkage linkage = fir::Linkage::External;
  fir::LinkVisibility vis = fir::LinkVisibility::Default;
  bool variadic = false;
  bool needs_register_save_area = false;
  // bytes the entry block reserves with `sub rsp` for its static allocas, every
  // return releases them again with an `add rsp`
  u32 static_alloca_size = 0;
  // returns restore rsp from a copy taken at the start of the entry block
  bool has_dynamic_alloca = false;
  // IRVec<Type> arg_tys;

  IRVec<StackSlot> extra_stack_slots;
  IRVec<MAsmBlock> asm_blocks;

  bool void_ret = true;
  u16 align = 0x10;
  Type res_ty = Type::INVALID;

  StackSlotId create_stack_slot(u64 size) {
    extra_stack_slots.push_back(StackSlot{size});
    return extra_stack_slots.size();
  }

  StackSlot &get_stack_slot(u64 stack_slot_id) {
    return extra_stack_slots[stack_slot_id - 1];
  }
};

}  // namespace foptim::fmir
