#include "legalization.hpp"
#include "mir/func.hpp"
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
  auto int_version = (ty == Type::Float64 || ty == Type::Int64) ? Type::Int64
                     : (ty == Type::Float32 || ty == Type::Int32)
                         ? Type::Int32
                         : Type::INVALID;
  ASSERT(int_version != Type::INVALID);
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
  auto int_version = (ty == Type::Float64 || ty == Type::Int64) ? Type::Int64
                     : (ty == Type::Float32 || ty == Type::Int32)
                         ? Type::Int32
                         : Type::INVALID;
  ASSERT(int_version != Type::INVALID);
  auto new_int_reg = get_reg(int_version);
  auto old_arg = bb.instrs[indx].args[arg_id];
  ASSERT(old_arg.isImm());
  bb.instrs[indx].args[arg_id] = new_int_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_int_reg, old_arg});
  return indx + 1;
}

u32 Legalizer::move_arg_to_pinned_reg(MBB &bb, u32 indx, u8 arg_id, Type ty,
                                      CReg vreg_ty) {
  auto new_reg = MArgument{VReg{vreg_ty, ty}, ty};
  auto old_arg = bb.instrs[indx].args[arg_id];
  bb.instrs[indx].args[arg_id] = new_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{Opcode::mov, new_reg, old_arg});
  return indx + 1;
}

bool Legalizer::legalize_icmp(MBB &bb, u32 indx) {
  MInstr &instr = bb.instrs[indx];
  bool big_unsigned_const =
      instr.args[1].isImm() &&
      instr.args[1].imm > (u64)std::numeric_limits<u16>::max();
  bool big_signed_const =
      instr.args[1].isImm() &&
      (i64)instr.args[1].imm > (i64)std::numeric_limits<i16>::max;

  bool big_unsigned_const2 =
      instr.args[2].isImm() &&
      instr.args[2].imm > (u64)std::numeric_limits<u16>::max();
  bool big_signed_const2 =
      instr.args[2].isImm() &&
      (i64)instr.args[2].imm > (i64)std::numeric_limits<i16>::max;

  switch (instr.op) {
  case Opcode::icmp_eq:
  case Opcode::icmp_ugt:
    if (big_unsigned_const2) {
      indx = move_arg_to_reg(bb, indx, 2, instr.args[1].ty);
      return true;
    }
    break;
  case Opcode::icmp_slt:
    if (big_signed_const2) {
      indx = move_arg_to_reg(bb, indx, 2, instr.args[1].ty);
      return true;
    }
    break;
  case Opcode::cjmp_int_ne:
  case Opcode::cjmp_int_eq:
  case Opcode::cjmp_int_ult:
    if (big_unsigned_const) {
      indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
      return true;
    }
    break;
  case Opcode::cjmp_int_slt:
    if (big_signed_const) {
      indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
      return true;
    }
    break;
  default:
  }
  return false;
}

bool Legalizer::legalize_fcmp(MBB &bb, u32 indx) {
  MInstr &instr = bb.instrs[indx];

  switch (instr.op) {
  case Opcode::cjmp_flt_oeq:
  case Opcode::cjmp_flt_ogt:
  case Opcode::cjmp_flt_oge:
  case Opcode::cjmp_flt_olt:
  case Opcode::cjmp_flt_ole:
  case Opcode::cjmp_flt_one:
  case Opcode::cjmp_flt_ord:
  case Opcode::cjmp_flt_uno:
  case Opcode::cjmp_flt_ueq:
  case Opcode::cjmp_flt_ugt:
  case Opcode::cjmp_flt_uge:
  case Opcode::cjmp_flt_ult:
  case Opcode::cjmp_flt_ule:
  case Opcode::cjmp_flt_une:
    if (instr.args[1].isImm()) {
      move_fp_const_to_reg(bb, indx, 1, instr.args[0].ty);
      return true;
    }
    break;
  case Opcode::fcmp_oeq:
  case Opcode::fcmp_ogt:
  case Opcode::fcmp_oge:
  case Opcode::fcmp_olt:
  case Opcode::fcmp_ole:
  case Opcode::fcmp_one:
  case Opcode::fcmp_ord:
  case Opcode::fcmp_uno:
  case Opcode::fcmp_ueq:
  case Opcode::fcmp_ugt:
  case Opcode::fcmp_uge:
  case Opcode::fcmp_ult:
  case Opcode::fcmp_ule:
  case Opcode::fcmp_une:
    if (instr.args[2].isImm()) {
      move_fp_const_to_reg(bb, indx, 2, instr.args[1].ty);
      return true;
    }
    break;
  default:
    break;
  }
  return false;
}

bool Legalizer::legalize_idiv(MBB &bb, u32 indx) {
  auto modified = false;
  {
    MInstr &instr = bb.instrs[indx];
    ASSERT(instr.args[0].isReg());
    ASSERT(instr.args[1].isReg());
    ASSERT(instr.args[0].reg.is_concrete());
    ASSERT(instr.args[1].reg.is_concrete());
    ASSERT(instr.args[0].reg.c_reg() == CReg::A);
    ASSERT(instr.args[1].reg.c_reg() == CReg::D);
    // dividend needs to be in eax to be extended into edx or rax for be
    // extended into rdx
    if (!instr.args[2].isReg() || !instr.args[2].reg.is_concrete() ||
        instr.args[2].reg.c_reg() != CReg::A) {
      indx = move_arg_to_pinned_reg(bb, indx, 2, instr.args[2].ty, CReg::A);
      modified = true;
    }
  }

  {
    MInstr &instr = bb.instrs[indx];
    if (!instr.args[3].isReg()) {
      // use the type of the result register since if this is a constant its
      // type might be to big
      indx = move_arg_to_reg(bb, indx, 3, instr.args[0].ty);
      modified = true;
    }
  }
  return modified;
}

bool Legalizer::legalize_push(MBB &bb, u32 indx) {
  {
    // cant push a 64bit imm value
    MInstr &instr = bb.instrs[indx];
    if (instr.args[0].isImm()) {
      if (instr.args[0].ty == Type::Float64 ||
          instr.args[0].ty == Type::Float32) {
        indx = move_fp_const_to_grp(bb, indx, 0, instr.args[0].ty);
        return true;
      }
      if (instr.args[0].ty == Type::Int64 || instr.args[0].ty == Type::Int32) {
        indx = move_arg_to_reg(bb, indx, 0, instr.args[0].ty);
        return true;
      }
    }
  }
  return false;
}

bool Legalizer::legalize_move(MBB &bb, u32 indx) {

  MInstr &instr = bb.instrs[indx];
  // cant move an immediate floating point value
  if (instr.args[0].isReg() && instr.args[0].reg.is_vec_reg() &&
      instr.args[1].isImm()) {
    // cant jsut check on == 0 on a floating point
    //  if (instr.args[1].is_fp() && instr.args[1].immf == 0) {
    //    instr.op = Opcode::fxor;
    //    instr.args[1] = instr.args[0];
    //    instr.args[2] = instr.args[0];
    //    instr.n_args = 3;
    //  } else {
    move_fp_const_to_grp(bb, indx, 1, instr.args[1].ty);
    // }
    return true;
  }

  // cannot move a 64bit constant into a mem
  if (instr.args[0].isMem() && instr.args[1].isImm() &&
      get_size(instr.args[0].ty) == 8) {
    if (instr.args[1].is_fp()) {
      move_fp_const_to_grp(bb, indx, 1, instr.args[0].ty);
    } else {
      move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
    }
    return true;
  }

  // we wanna transform every one/two byte load into a movzx;
  //  this awoids a false dependency and also clears the upper half with zero
  //  which then makes further usage easier
  if (instr.op == Opcode::mov && instr.args[0].isReg() &&
      (instr.args[0].ty == Type::Int8 || instr.args[0].ty == Type::Int16) &&
      instr.args[1].isMem()) {
    instr.op = Opcode::mov_zx;
    instr.args[0].ty = Type::Int32;
    instr.args[0].reg.ty = Type::Int32;
    return true;
  }
  if (instr.op == Opcode::mov && instr.args[0].isReg() &&
      instr.args[1].isReg() && !instr.args[0].is_fp() &&
      instr.args[0].ty != instr.args[1].ty) {
    auto t0 = instr.args[0].ty;
    auto t1 = instr.args[1].ty;
    if (get_size(t0) > get_size(t1)) {
      instr.op = Opcode::mov_zx;
      return true;
    } else {
      instr.op = Opcode::itrunc;
      return true;
    }
    // ASSERT(get_size(t0) > get_size(t1));
    // instr.args[1].ty = t1;
    // instr.args[1].reg.info.reg_size = 4;
    // instr.args[1].reg.c_reg();
  }
  if (instr.op == Opcode::mov_zx && instr.args[1].isImm()) {
    instr.op = Opcode::mov;
    instr.args[1].ty = instr.args[0].ty;
    return true;
  }
  if (instr.op == Opcode::mov_sx && instr.args[1].isImm()) {
    instr.op = Opcode::mov;
    i64 val_big;
    switch (instr.args[1].ty) {
    case Type::INVALID:
    case Type::Int8: {
      i8 val_smol = static_cast<i8>(static_cast<u8>(instr.args[1].imm));
      val_big = static_cast<i64>(val_smol);
      break;
    }
    case Type::Int16: {
      i16 val_smol = static_cast<i16>(static_cast<u16>(instr.args[1].imm));
      val_big = static_cast<i64>(val_smol);
      break;
    }
    case Type::Int32: {
      i32 val_smol = static_cast<i32>(static_cast<u32>(instr.args[1].imm));
      val_big = static_cast<i64>(val_smol);
      break;
    }
    case Type::Int64:
    case Type::Float32:
    case Type::Float64:
      TODO("IMPL");
    }
    instr.args[1].imm = (u64)val_big;
    instr.args[1].ty = instr.args[0].ty;
    return true;
  }
  return false;
}

bool Legalizer::legalize_arg_setup(MBB &bb, u32 indx) {
  bool modified = false;
  {
    // NOTE: just like push for now
    //  cant push a 64bit imm value

    // only apply to first
    if (indx != 0 && bb.instrs[indx - 1].op == Opcode::arg_setup) {
      return false;
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
      // we only need to do this if we use the stack
      // since pushing some stuff will be problematic
      if (!instr.args[0].is_fp() && instr.n_args > 1) {
        continue;
      }
      if (instr.args[0].ty != Type::Float64 &&
          instr.args[0].ty != Type::Float32 &&
          instr.args[0].imm <= (u64)std::numeric_limits<i32>::max()) {
        continue;
      }

      auto ty = instr.args[0].ty;
      // auto res_ty = ty == Type::Float64   ? Type::Int64
      //               : ty == Type::Float32 ? Type::Int32
      //                                     : ty;
      auto new_reg = get_reg(ty);
      // new_reg.ty = ty;
      auto old_arg = instr.args[0];
      instr.args[0] = new_reg;
      changes.emplace_back(Opcode::mov, new_reg, old_arg);
    }

    for (auto change : changes) {
      bb.instrs.insert(bb.instrs.begin() + insert_index, change);
      modified = true;
    }
  }
  return modified;
}

bool Legalizer::legalize_floating_binary_ops(MBB &bb, u32 indx) {
  MInstr &instr = bb.instrs[indx];
  if (instr.args[1].isImm()) {
    indx = move_fp_const_to_reg(bb, indx, 1, instr.args[0].ty);
    return true;
  }
  if (instr.args[2].isImm()) {
    indx = move_fp_const_to_reg(bb, indx, 2, instr.args[0].ty);
    return true;
  }
  return false;
}

// bool Legalizer::legalize_sub(MBB &bb, u32 indx) {
//   bool modified = false;
//   // {
//   //   MInstr &instr = bb.instrs[indx];
//   //   if (instr.args[0] == instr.args[2]) {
//   //     indx = move_arg_to_reg(bb, indx, 2, instr.args[0].ty);
//   //     modified = true;
//   //   }
//   // }
//   return modified;
// }

bool Legalizer::legalize_cmove(MBB &bb, u32 indx) {
  {
    // 2nd arg cant be a constant
    MInstr &instr = bb.instrs[indx];
    if (instr.args[2].isImm()) {
      if (instr.args[2].ty == Type::Float32 ||
          instr.args[2].ty == Type::Float64) {
        indx = move_fp_const_to_reg(bb, indx, 2, instr.args[0].ty);
      } else {
        indx = move_arg_to_reg(bb, indx, 2, instr.args[0].ty);
      }
      return true;
    }
  }
  return false;
}

bool Legalizer::legalize_conversion(MBB &bb, u32 indx) {
  MInstr &instr = bb.instrs[indx];
  if (instr.op == Opcode::SI2FL && instr.args[1].isImm()) {
    indx = move_arg_to_reg(bb, indx, 1, instr.args[1].ty);
    return true;
  }
  if (instr.op == Opcode::UI2FL && instr.args[1].isImm()) {
    indx = move_arg_to_reg(bb, indx, 1, instr.args[1].ty);
    return true;
  }
  if (instr.op == Opcode::FL2UI && instr.args[1].isImm()) {
    indx = move_arg_to_reg(bb, indx, 1, instr.args[1].ty);
    return true;
  }
  if (instr.op == Opcode::UI2FL && instr.args[1].isReg() &&
      get_size(instr.args[1].ty) < 4) {
    auto new_reg = get_reg(Type::Int32);
    auto old_arg = bb.instrs[indx].args[1];
    bb.instrs[indx].args[1] = new_reg;
    bb.instrs.insert(bb.instrs.begin() + indx,
                     MInstr{Opcode::mov_zx, new_reg, old_arg});
    return true;
  }
  return false;
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
          if (!instr.args[i].reg.is_concrete()) {
            unique_reg_id =
                std::max(unique_reg_id, instr.args[i].reg.virt_id());
          }
          break;
        case MArgument::ArgumentType::MemImmVRegScale:
          if (!instr.args[i].indx.is_concrete()) {
            unique_reg_id =
                std::max(unique_reg_id, instr.args[i].indx.virt_id());
          }
          break;
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          if (!instr.args[i].reg.is_concrete()) {
            unique_reg_id =
                std::max(unique_reg_id, instr.args[i].reg.virt_id());
          }
          if (!instr.args[i].indx.is_concrete()) {
            unique_reg_id =
                std::max(unique_reg_id, instr.args[i].indx.virt_id());
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
    for (size_t ioff = 1; ioff <= bb.instrs.size(); ioff++) {
      auto i = ioff - 1;
      switch (bb.instrs[i].op) {
      case Opcode::mov:
      case Opcode::mov_zx:
      case Opcode::mov_sx:
        if (legalize_move(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::icmp_ugt:
      case Opcode::icmp_slt:
      case Opcode::icmp_eq:
      case Opcode::cjmp_int_slt:
      case Opcode::cjmp_int_ult:
      case Opcode::cjmp_int_ne:
      case Opcode::cjmp_int_eq:
        if (legalize_icmp(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::cjmp_flt_oeq:
      case Opcode::cjmp_flt_ogt:
      case Opcode::cjmp_flt_oge:
      case Opcode::cjmp_flt_olt:
      case Opcode::cjmp_flt_ole:
      case Opcode::cjmp_flt_one:
      case Opcode::cjmp_flt_ord:
      case Opcode::cjmp_flt_uno:
      case Opcode::cjmp_flt_ueq:
      case Opcode::cjmp_flt_ugt:
      case Opcode::cjmp_flt_uge:
      case Opcode::cjmp_flt_ult:
      case Opcode::cjmp_flt_ule:
      case Opcode::cjmp_flt_une:
      case Opcode::fcmp_oeq:
      case Opcode::fcmp_ogt:
      case Opcode::fcmp_oge:
      case Opcode::fcmp_olt:
      case Opcode::fcmp_ole:
      case Opcode::fcmp_one:
      case Opcode::fcmp_ord:
      case Opcode::fcmp_uno:
      case Opcode::fcmp_ueq:
      case Opcode::fcmp_ugt:
      case Opcode::fcmp_uge:
      case Opcode::fcmp_ult:
      case Opcode::fcmp_ule:
      case Opcode::fcmp_une:
        if (legalize_fcmp(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::udiv:
      case Opcode::idiv:
        if (legalize_idiv(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::UI2FL:
      case Opcode::FL2UI:
      case Opcode::FL2SI:
      case Opcode::SI2FL:
        if (legalize_conversion(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::fmul:
      case Opcode::fdiv:
      case Opcode::fsub:
      case Opcode::fxor:
      case Opcode::fadd:
      case Opcode::fAnd:
      case Opcode::fOr:
        if (legalize_floating_binary_ops(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::push:
        if (legalize_push(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::arg_setup:
        if (legalize_arg_setup(bb, i)) {
          ioff = 0;
        }
        break;
      case Opcode::cmov:
        if (legalize_cmove(bb, i)) {
          ioff = 0;
        }
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
