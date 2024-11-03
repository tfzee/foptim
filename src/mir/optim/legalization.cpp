#include "legalization.hpp"
#include "mir/instr.hpp"
#include <limits>

namespace foptim::fmir {

MArgument Legalizer::get_reg(Type type) {
  auto size = get_size(type);
  ASSERT(size <= 255);
  return {VReg{unique_reg_id++, (u8)size}, type};
}

u32 Legalizer::move_arg_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty) {
  auto new_reg = get_reg(ty);
  auto old_arg = bb.instrs[indx].args[arg_id];
  bb.instrs[indx].args[arg_id] = new_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_reg, old_arg});
  return indx + 1;
}

u32 Legalizer::move_arg_to_pinned_reg(MBB &bb, u32 indx, u8 arg_id, Type ty,
                                      VRegType vreg_ty) {
  auto size = get_size(ty);
  ASSERT(size <= 255);
  auto new_reg = MArgument{VReg{0, VRegInfo{vreg_ty, (u8)size}}, ty};
  auto old_arg = bb.instrs[indx].args[arg_id];
  bb.instrs[indx].args[arg_id] = new_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_reg, old_arg});
  return indx + 1;
}

void Legalizer::legalize_cmp(MBB &bb, u32 indx) {
  MInstr &instr = bb.instrs[indx];
  if (instr.args[1].isImm()) {
    bool big_unsigned_const =
        instr.args[1].imm > (u64)std::numeric_limits<u16>::max();
    bool big_signed_const =
        (i64)instr.args[1].imm > (i64)std::numeric_limits<i16>::max;

    switch (instr.op) {
    case Opcode::icmp_eq:
      if (big_unsigned_const) {
        indx = move_arg_to_reg(bb, indx, 2, instr.args[1].ty);
        return;
      }
    case Opcode::cjmp_ne:
    case Opcode::cjmp_eq:
    case Opcode::cjmp_ult:
      if (big_unsigned_const) {
        indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
        return;
      }
    case Opcode::icmp_slt:
      if (big_signed_const) {
        indx = move_arg_to_reg(bb, indx, 2, instr.args[1].ty);
        return;
      }
    case Opcode::cjmp_slt:
      if (big_signed_const) {
        indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
        return;
      }
    default:
    }
  }
}

void Legalizer::legalize_idiv(MBB &bb, u32 indx) {
  {
    MInstr &instr = bb.instrs[indx];
    ASSERT(instr.args[0].isReg());
    ASSERT(instr.args[1].isReg());
    ASSERT(instr.args[0].reg.info.ty == VRegType::A);
    ASSERT(instr.args[1].reg.info.ty == VRegType::D);
    utils::Debug << "INSTR: " << bb.instrs[indx] << "\n";
    // dividend needs to be in eax to be extended into edx or rax for be
    // extended into rdx
    if (!instr.args[2].isReg() || instr.args[2].reg.info.ty != VRegType::A) {
      indx = move_arg_to_pinned_reg(bb, indx, 2, instr.args[2].ty, VRegType::A);
    }
  }

  {
    MInstr &instr = bb.instrs[indx];
    if (!instr.args[3].isReg()) {
      indx = move_arg_to_reg(bb, indx, 3, instr.args[3].ty);
    }
  }
}

void Legalizer::apply(MFunc &func) {
  unique_reg_id = 0;
  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      for (u8 i = 0; i < instr.n_args; i++) {
        switch (instr.args[i].type) {
        case MArgument::ArgumentType::VReg:
        case MArgument::ArgumentType::MemVReg:
        case MArgument::ArgumentType::MemImmVReg:
          if (!instr.args[i].reg.info.is_pinned()) {
            unique_reg_id = std::max(unique_reg_id, instr.args[i].reg.id);
          }
          break;
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          if (!instr.args[i].reg.info.is_pinned()) {
            unique_reg_id = std::max(unique_reg_id, instr.args[i].reg.id);
          }
          if (!instr.args[i].indx.info.is_pinned()) {
            unique_reg_id = std::max(unique_reg_id, instr.args[i].indx.id);
          }
          break;
        default:
          break;
        }
      }
    }
  }
  unique_reg_id++;

  for (auto &bb : func.bbs) {
    for (size_t i = 0; i < bb.instrs.size(); i++) {
      switch (bb.instrs[i].op) {
      case Opcode::icmp_slt:
      case Opcode::icmp_eq:
      case Opcode::cjmp_slt:
      case Opcode::cjmp_ult:
      case Opcode::cjmp_ne:
      case Opcode::cjmp_eq:
        legalize_cmp(bb, i);
        break;
      case Opcode::idiv:
        legalize_idiv(bb, i);
        break;
      default:
        break;
      }
    }
  }
}

void Legalizer::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("MIR Legalizer");
  for (auto &func : funcs) {
    apply(func);
  }
}

} // namespace foptim::fmir
