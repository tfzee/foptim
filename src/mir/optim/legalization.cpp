#include "legalization.hpp"
#include "mir/instr.hpp"
#include <limits>

namespace foptim::fmir {

MArgument Legalizer::get_reg(Type type) {
  // auto size = get_size(type);
  // ASSERT(size <= 255);
  // bool is_float = type == Type::Float32 || type == Type::Float64;
  return {VReg{unique_reg_id++, type}, type};
}

u32 Legalizer::move_arg_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty) {
  auto new_reg = get_reg(ty);
  auto old_arg = bb.instrs[indx].args[arg_id];
  bb.instrs[indx].args[arg_id] = new_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_reg, old_arg});
  return indx + 1;
}

u32 Legalizer::move_fp_const_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty) {
  auto int_version = ty == Type::Float64   ? Type::Int64
                     : ty == Type::Float32 ? Type::Int32
                                           : Type::INVALID;
  auto new_int_reg = get_reg(int_version);
  auto new_float_reg = get_reg(ty);
  auto old_arg = bb.instrs[indx].args[arg_id];
  ASSERT(old_arg.isImm());
  // Update the type since it is a constant and its type might be bigger
  old_arg.ty = ty;
  bb.instrs[indx].args[arg_id] = new_float_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_float_reg, new_int_reg});
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_int_reg, old_arg});
  return indx + 2;
}

u32 Legalizer::move_fp_const_to_grp(MBB &bb, u32 indx, u8 arg_id, Type ty) {
  auto int_version = ty == Type::Float64   ? Type::Int64
                     : ty == Type::Float32 ? Type::Int32
                                           : Type::INVALID;
  auto new_int_reg = get_reg(int_version);
  auto old_arg = bb.instrs[indx].args[arg_id];
  ASSERT(old_arg.isImm());
  bb.instrs[indx].args[arg_id] = new_int_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_int_reg, old_arg});
  return indx + 1;
}

u32 Legalizer::move_arg_to_pinned_reg(MBB &bb, u32 indx, u8 arg_id, Type ty,
                                      VRegType vreg_ty) {
  auto new_reg = MArgument{VReg{0, VRegInfo{vreg_ty, ty}}, ty};
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
    case Opcode::cjmp_int_ne:
    case Opcode::cjmp_int_eq:
    case Opcode::cjmp_int_ult:
      if (big_unsigned_const) {
        indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
        return;
      }
    case Opcode::icmp_slt:
      if (big_signed_const) {
        indx = move_arg_to_reg(bb, indx, 2, instr.args[1].ty);
        return;
      }
    case Opcode::cjmp_int_slt:
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
    // utils::Debug << "INSTR: " << bb.instrs[indx] << "\n";
    // dividend needs to be in eax to be extended into edx or rax for be
    // extended into rdx
    if (!instr.args[2].isReg() || instr.args[2].reg.info.ty != VRegType::A) {
      indx = move_arg_to_pinned_reg(bb, indx, 2, instr.args[2].ty, VRegType::A);
    }
  }

  {
    MInstr &instr = bb.instrs[indx];
    if (!instr.args[3].isReg()) {
      // use the type of the result register since if this is a constant its
      // type might be to big
      indx = move_arg_to_reg(bb, indx, 3, instr.args[0].ty);
    }
  }
}

void Legalizer::legalize_push(MBB &bb, u32 indx) {
  {
    // cant push a 64bit imm value
    MInstr &instr = bb.instrs[indx];
    if (instr.args[0].isImm()) {
      if (instr.args[0].ty == Type::Float64 ||
          instr.args[0].ty == Type::Float32) {
        indx = move_fp_const_to_grp(bb, indx, 0, instr.args[0].ty);
        return;
      }
      if (instr.args[0].ty == Type::Int64 || instr.args[0].ty == Type::Int32) {
        indx = move_arg_to_reg(bb, indx, 0, instr.args[0].ty);
        return;
      }
    }
  }
}

void Legalizer::legalize_arg_setup(MBB &bb, u32 indx) {
  {
    // NOTE: just like push for now
    //  cant push a 64bit imm value

    // only apply to first
    if (indx != 0 && bb.instrs[indx - 1].op == Opcode::arg_setup) {
      return;
    }

    auto insert_index = indx;
    auto end_indx = indx;
    for (auto i = insert_index; i < bb.instrs.size(); i++) {
      if (bb.instrs[i].op == Opcode::invoke) {
        end_indx = i;
        break;
      }
    }
    ASSERT(end_indx != insert_index);

    TVec<MInstr> changes;
    for (auto i = insert_index; i < end_indx; ++i) {
      auto &instr = bb.instrs[i];
      if (!instr.args[0].isImm()) {
        continue;
      }
      if (instr.args[0].ty != Type::Float64 &&
          instr.args[0].ty != Type::Float32 &&
          instr.args[0].imm <= (u64)std::numeric_limits<i32>::max()) {
        continue;
      }

      auto ty = instr.args[0].ty;
      auto res_ty = ty == Type::Float64   ? Type::Int64
                    : ty == Type::Float32 ? Type::Int32
                                          : ty;
      auto new_reg = get_reg(res_ty);
      auto old_arg = instr.args[0];
      instr.args[0] = new_reg;
      changes.emplace_back(Opcode::mov, new_reg, old_arg);
    }

    for (auto change : changes) {
      bb.instrs.insert(bb.instrs.begin() + insert_index, change);
    }

    // MInstr &instr = bb.instrs[indx];
    // if (instr.args[0].isImm()) {
    //   if (instr.args[0].ty == Type::Float64 ||
    //       instr.args[0].ty == Type::Float32) {
    //     indx = move_fp_const_to_grp(bb, indx, 0, instr.args[0].ty);
    //     return;
    //   }
    //   if (instr.args[0].imm >= (u64)std::numeric_limits<i32>::max()) {
    //     utils::Debug << "LEG" << bb.instrs[indx - 1] << " " <<
    //     bb.instrs[indx]
    //                  << "\n";
    //     indx = move_arg_to_reg(bb, indx, 0, instr.args[0].ty);
    //     return;
    //   }
    // }
  }
}

void Legalizer::legalize_fadd(MBB &bb, u32 indx) {
  {
    // 2nd arg cant be a constant
    // and loading floating constants is a pain
    MInstr &instr = bb.instrs[indx];
    if (instr.args[2].isImm()) {
      indx = move_fp_const_to_reg(bb, indx, 2, instr.args[0].ty);
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
      case Opcode::cjmp_int_slt:
      case Opcode::cjmp_int_ult:
      case Opcode::cjmp_int_ne:
      case Opcode::cjmp_int_eq:
        legalize_cmp(bb, i);
        break;
      case Opcode::idiv:
        legalize_idiv(bb, i);
        break;
      case Opcode::fadd:
        legalize_fadd(bb, i);
        break;
      case Opcode::push:
        legalize_push(bb, i);
        break;
      case Opcode::arg_setup:
        legalize_arg_setup(bb, i);
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
