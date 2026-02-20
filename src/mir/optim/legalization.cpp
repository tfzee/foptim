#include "legalization.hpp"

#include <limits>

#include "mir/func.hpp"
#include "mir/instr.hpp"

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
                   MInstr{GBaseSubtype::mov, new_reg, old_arg});
  return indx + 1;
}

u32 Legalizer::move_fp_const_to_reg(MBB &bb, u32 indx, u8 arg_id, Type ty) {
  Type int_version = Type::INVALID;
  switch (ty) {
    case Type::INVALID:
    case Type::Int8:
    case Type::Int16:
    case Type::Int32:
    case Type::Int64:
    case fmir::Type::Float32x16:
    case fmir::Type::Float64x8:
      TODO("UNREACH");
    case Type::Int32x4:
    case Type::Float32x2:
    case Type::Float32x4:
    case Type::Int32x8:
    case Type::Float32x8:
    case Type::Float32:
      int_version = Type::Int32;
      break;
    case Type::Int64x2:
    case Type::Float64x2:
    case Type::Int64x4:
    case Type::Float64x4:
    case Type::Float64:
      int_version = Type::Int64;
      break;
  }
  auto old_arg = bb.instrs[indx].args[arg_id];
  auto new_float_reg = get_reg(ty);
  ASSERT(old_arg.isImm());
  if (std::bit_cast<u64>(old_arg.immf) == 0) {
    bb.instrs[indx].args[arg_id] = new_float_reg;
    bb.instrs.insert(
        bb.instrs.begin() + indx,
        MInstr{GVecSubtype::fxor, new_float_reg, new_float_reg, new_float_reg});
    return indx + 1;
  }

  ASSERT(int_version != Type::INVALID);
  auto new_int_reg = get_reg(int_version);
  // Update the type since it is a constant and its type might be bigger
  old_arg.ty = ty;
  bb.instrs[indx].args[arg_id] = new_float_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{GBaseSubtype::mov, new_float_reg, new_int_reg});
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{GBaseSubtype::mov, new_int_reg, old_arg});
  return indx + 2;
}

u32 Legalizer::move_fp_const_to_grp(MBB &bb, u32 indx, u8 arg_id, Type ty) {
  Type int_version = Type::INVALID;
  switch (ty) {
    case Type::INVALID:
    case Type::Int8:
    case Type::Int16:
    case fmir::Type::Float32x16:
    case fmir::Type::Float64x8:
      fmt::println("{}", bb);
      TODO("UNREACH");
    case Type::Int32:
    case Type::Int32x4:
    case Type::Float32x2:
    case Type::Float32x4:
    case Type::Int32x8:
    case Type::Float32x8:
    case Type::Float32:
      int_version = Type::Int32;
      break;
    case Type::Int64:
    case Type::Int64x2:
    case Type::Float64x2:
    case Type::Int64x4:
    case Type::Float64x4:
    case Type::Float64:
      int_version = Type::Int64;
      break;
  }
  ASSERT(int_version != Type::INVALID);
  auto new_int_reg = get_reg(int_version);
  auto old_arg = bb.instrs[indx].args[arg_id];
  if (old_arg.immf == 0) {
    bb.instrs[indx].args[arg_id] = new_int_reg;
  }
  ASSERT(old_arg.isImm());
  bb.instrs[indx].args[arg_id] = new_int_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{GBaseSubtype::mov, new_int_reg, old_arg});
  return indx + 1;
}

u32 Legalizer::move_arg_to_pinned_reg(MBB &bb, u32 indx, u8 arg_id, Type ty,
                                      CReg vreg_ty) {
  auto new_reg = MArgument{VReg{vreg_ty, ty}, ty};
  auto old_arg = bb.instrs[indx].args[arg_id];
  bb.instrs[indx].args[arg_id] = new_reg;
  bb.instrs.insert(bb.instrs.begin() + indx,
                   MInstr{GBaseSubtype::mov, new_reg, old_arg});
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

  if (instr.bop == GOpcode::GJmp) {
    switch ((GJumpSubtype)instr.sop) {
      case GJumpSubtype::icmp_eq:
      case GJumpSubtype::icmp_ugt:
        if (big_unsigned_const2) {
          indx = move_arg_to_reg(bb, indx, 2, instr.args[1].ty);
          return true;
        }
        break;
      case GJumpSubtype::icmp_slt:
        if (big_signed_const2) {
          indx = move_arg_to_reg(bb, indx, 2, instr.args[1].ty);
          return true;
        }
        break;
      case GJumpSubtype::cjmp_int_ne:
      case GJumpSubtype::cjmp_int_eq:
      case GJumpSubtype::cjmp_int_ult:
      case GJumpSubtype::cjmp_int_ugt:
      case GJumpSubtype::cjmp_int_ule:
      case GJumpSubtype::cjmp_int_uge:
        if (big_unsigned_const) {
          indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
          return true;
        }
        break;
      case GJumpSubtype::cjmp_int_slt:
      case GJumpSubtype::cjmp_int_sgt:
      case GJumpSubtype::cjmp_int_sge:
      case GJumpSubtype::cjmp_int_sle:
        if (big_signed_const) {
          indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
          return true;
        }
        break;
      default:
    }
  }
  return false;
}

bool Legalizer::legalize_fcmp(MBB &bb, u32 indx) {
  MInstr &instr = bb.instrs[indx];

  if (instr.bop == GOpcode::GJmp) {
    switch ((GJumpSubtype)instr.sop) {
      case GJumpSubtype::cjmp_flt_oeq:
      case GJumpSubtype::cjmp_flt_ogt:
      case GJumpSubtype::cjmp_flt_oge:
      case GJumpSubtype::cjmp_flt_olt:
      case GJumpSubtype::cjmp_flt_ole:
      case GJumpSubtype::cjmp_flt_one:
      case GJumpSubtype::cjmp_flt_ord:
      case GJumpSubtype::cjmp_flt_uno:
      case GJumpSubtype::cjmp_flt_ueq:
      case GJumpSubtype::cjmp_flt_ugt:
      case GJumpSubtype::cjmp_flt_uge:
      case GJumpSubtype::cjmp_flt_ult:
      case GJumpSubtype::cjmp_flt_ule:
      case GJumpSubtype::cjmp_flt_une:
        if (instr.args[1].isImm()) {
          move_fp_const_to_reg(bb, indx, 1, instr.args[0].ty);
          return true;
        }
        break;
      case GJumpSubtype::fcmp_oeq:
      case GJumpSubtype::fcmp_ogt:
      case GJumpSubtype::fcmp_oge:
      case GJumpSubtype::fcmp_olt:
      case GJumpSubtype::fcmp_ole:
      case GJumpSubtype::fcmp_one:
      case GJumpSubtype::fcmp_ord:
      case GJumpSubtype::fcmp_uno:
      case GJumpSubtype::fcmp_ueq:
      case GJumpSubtype::fcmp_ugt:
      case GJumpSubtype::fcmp_uge:
      case GJumpSubtype::fcmp_ult:
      case GJumpSubtype::fcmp_ule:
      case GJumpSubtype::fcmp_une:
        if (instr.args[2].isImm()) {
          move_fp_const_to_reg(bb, indx, 2, instr.args[1].ty);
          return true;
        }
        break;
      default:
        break;
    }
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
    if (instr.args[1].is_fp() && instr.args[1].immf == 0) {
      instr.bop = GOpcode::GVec;
      instr.sop = (u32)GVecSubtype::fxor;
      instr.args[1] = instr.args[0];
      instr.args[2] = instr.args[0];
      instr.n_args = 3;
    } else {
      move_fp_const_to_grp(bb, indx, 1, instr.args[1].ty);
    }
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
  if (instr.is(GBaseSubtype::mov) && instr.args[0].isReg() &&
      (instr.args[0].ty == Type::Int8 || instr.args[0].ty == Type::Int16) &&
      instr.args[1].isMem()) {
    instr.bop = GOpcode::GConv;
    instr.sop = (u32)GConvSubtype::mov_zx;
    instr.args[0].ty = Type::Int32;
    instr.args[0].reg.ty = Type::Int32;
    return true;
  }
  if (instr.is(GBaseSubtype::mov) && instr.args[0].isReg() &&
      instr.args[1].isReg() && !instr.args[0].is_vec_reg() &&
      !instr.args[1].is_vec_reg() && instr.args[0].ty != instr.args[1].ty) {
    auto t0 = instr.args[0].ty;
    auto t1 = instr.args[1].ty;
    if (get_size(t0) > get_size(t1)) {
      instr.bop = GOpcode::GConv;
      instr.sop = (u32)GConvSubtype::mov_zx;
      return true;
    }
    instr.bop = GOpcode::GConv;
    instr.sop = (u32)GConvSubtype::itrunc;
    return true;

    // ASSERT(get_size(t0) > get_size(t1));
    // instr.args[1].ty = t1;
    // instr.args[1].reg.info.reg_size = 4;
    // instr.args[1].reg.c_reg();
  }
  if (instr.is(GConvSubtype::mov_zx) && instr.args[1].isImm()) {
    instr.bop = GOpcode::GBase;
    instr.sop = (u32)GBaseSubtype::mov;
    instr.args[1].ty = instr.args[0].ty;
    return true;
  }
  if (instr.is(GConvSubtype::mov_sx) && instr.args[1].isImm()) {
    instr.bop = GOpcode::GBase;
    instr.sop = (u32)GBaseSubtype::mov;
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
      default:
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
    if (indx != 0 && bb.instrs[indx - 1].is(GBaseSubtype::arg_setup)) {
      return false;
    }

    auto insert_index = indx;
    auto end_indx = indx;
    for (auto i = insert_index; i < bb.instrs.size(); i++) {
      if (bb.instrs[i].is(GBaseSubtype::invoke)) {
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
      changes.emplace_back(GBaseSubtype::mov, new_reg, old_arg);
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

bool Legalizer::legalize_arith_op(MBB &bb, u32 indx) {
  {
    // 2nd arg cant be a 64bit constant
    MInstr &instr = bb.instrs[indx];
    if (instr.args[1].isImm() &&
        instr.args[1].imm > std::numeric_limits<i32>::max()) {
      indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
      return true;
    }
  }
  return false;
}

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

bool Legalizer::legalize_cmoveXX(MBB &bb, u32 indx) {
  MInstr &instr = bb.instrs[indx];
  {
    // 2nd arg cant be a constant
    if (instr.args[1].isImm()) {
      if (instr.args[1].ty == Type::Float32 ||
          instr.args[1].ty == Type::Float64) {
        indx = move_fp_const_to_reg(bb, indx, 1, instr.args[0].ty);
      } else {
        indx = move_arg_to_reg(bb, indx, 1, instr.args[0].ty);
      }
      return true;
    }
  }

  {  // cant have big constants in comparison
    bool big_unsigned_const =
        instr.args[3].isImm() &&
        instr.args[3].imm > (u64)std::numeric_limits<u16>::max();
    bool big_signed_const =
        instr.args[3].isImm() &&
        (i64)instr.args[3].imm > (i64)std::numeric_limits<i16>::max;

    if (instr.bop == GOpcode::GCMov) {
      switch ((GCMovSubtype)instr.sop) {
        case GCMovSubtype::cmov_ne:
        case GCMovSubtype::cmov_eq:
        case GCMovSubtype::cmov_ult:
        case GCMovSubtype::cmov_ugt:
        case GCMovSubtype::cmov_ule:
        case GCMovSubtype::cmov_uge:
          if (big_unsigned_const) {
            indx = move_arg_to_reg(bb, indx, 3, instr.args[2].ty);
            return true;
          }
          break;
        case GCMovSubtype::cmov_slt:
        case GCMovSubtype::cmov_sgt:
        case GCMovSubtype::cmov_sge:
        case GCMovSubtype::cmov_sle:
          if (big_signed_const) {
            indx = move_arg_to_reg(bb, indx, 3, instr.args[2].ty);
            return true;
          }
          break;
        default:
      }
    }
  }
  {  // cant have big constants in comparison
    bool big_unsigned_const =
        instr.args[2].isImm() &&
        instr.args[2].imm > (u64)std::numeric_limits<u16>::max();
    bool big_signed_const =
        instr.args[2].isImm() &&
        (i64)instr.args[2].imm > (i64)std::numeric_limits<i16>::max;

    if (instr.bop == GOpcode::GCMov) {
      switch ((GCMovSubtype)instr.sop) {
        case GCMovSubtype::cmov_ne:
        case GCMovSubtype::cmov_eq:
        case GCMovSubtype::cmov_ult:
        case GCMovSubtype::cmov_ugt:
        case GCMovSubtype::cmov_ule:
        case GCMovSubtype::cmov_uge:
          if (big_unsigned_const) {
            indx = move_arg_to_reg(bb, indx, 2, instr.args[3].ty);
            return true;
          }
          break;
        case GCMovSubtype::cmov_slt:
        case GCMovSubtype::cmov_sgt:
        case GCMovSubtype::cmov_sge:
        case GCMovSubtype::cmov_sle:
          if (big_signed_const) {
            indx = move_arg_to_reg(bb, indx, 2, instr.args[3].ty);
            return true;
          }
          break;
        default:
      }
    }
  }
  return false;
}

bool Legalizer::legalize_punpckl(MBB &bb, u32 indx) {
  {
    // 2nd arg cant be a constant
    MInstr &instr = bb.instrs[indx];
    if (instr.args[2].isImm()) {
      if (instr.args[2].ty >= Type::Float32) {
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
  if (instr.is(GConvSubtype::SI2FL) && instr.args[1].isReg() &&
      get_size(instr.args[1].ty) < 4) {
    instr.args[1].ty = Type::Int32;
    instr.args[1].reg.ty = Type::Int32;
  }
  if (instr.is(GConvSubtype::SI2FL) && instr.args[1].isImm()) {
    indx = move_arg_to_reg(bb, indx, 1, instr.args[1].ty);
    return true;
  }
  if (instr.is(GConvSubtype::UI2FL) && instr.args[1].isImm()) {
    indx = move_arg_to_reg(bb, indx, 1, instr.args[1].ty);
    return true;
  }
  if (instr.is(GConvSubtype::FL2UI) && instr.args[1].isImm()) {
    indx = move_arg_to_reg(bb, indx, 1, instr.args[1].ty);
    return true;
  }
  if (instr.is(GConvSubtype::UI2FL) && instr.args[1].isReg() &&
      get_size(instr.args[1].ty) < 4) {
    auto new_reg = get_reg(Type::Int32);
    auto old_arg = bb.instrs[indx].args[1];
    bb.instrs[indx].args[1] = new_reg;
    bb.instrs.insert(bb.instrs.begin() + indx,
                     MInstr{GConvSubtype::mov_zx, new_reg, old_arg});
    return true;
  }
  return false;
}

void Legalizer::apply_impl(MFunc &func) {
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
      switch (bb.instrs[i].bop) {
        case GOpcode::GBase:
          switch ((GBaseSubtype)bb.instrs[i].sop) {
            case GBaseSubtype::mov:
              if (legalize_move(bb, i)) {
                ioff = 0;
              }
              break;
            case GBaseSubtype::push:
              if (legalize_push(bb, i)) {
                ioff = 0;
              }
              break;
            case GBaseSubtype::arg_setup:
              if (legalize_arg_setup(bb, i)) {
                ioff = 0;
              }
              break;
            default:
              break;
          }
          break;
        case GOpcode::GJmp:
          switch ((GJumpSubtype)bb.instrs[i].sop) {
            case GJumpSubtype::icmp_slt:
            case GJumpSubtype::icmp_eq:
            case GJumpSubtype::icmp_ult:
            case GJumpSubtype::icmp_ne:
            case GJumpSubtype::icmp_sgt:
            case GJumpSubtype::icmp_ugt:
            case GJumpSubtype::icmp_uge:
            case GJumpSubtype::icmp_ule:
            case GJumpSubtype::icmp_sge:
            case GJumpSubtype::icmp_sle:
            case GJumpSubtype::cjmp_int_slt:
            case GJumpSubtype::cjmp_int_sge:
            case GJumpSubtype::cjmp_int_sle:
            case GJumpSubtype::cjmp_int_sgt:
            case GJumpSubtype::cjmp_int_ult:
            case GJumpSubtype::cjmp_int_ule:
            case GJumpSubtype::cjmp_int_ugt:
            case GJumpSubtype::cjmp_int_uge:
            case GJumpSubtype::cjmp_int_ne:
            case GJumpSubtype::cjmp_int_eq:
              if (legalize_icmp(bb, i)) {
                ioff = 0;
              }
              break;
            case GJumpSubtype::cjmp_flt_oeq:
            case GJumpSubtype::cjmp_flt_ogt:
            case GJumpSubtype::cjmp_flt_oge:
            case GJumpSubtype::cjmp_flt_olt:
            case GJumpSubtype::cjmp_flt_ole:
            case GJumpSubtype::cjmp_flt_one:
            case GJumpSubtype::cjmp_flt_ord:
            case GJumpSubtype::cjmp_flt_uno:
            case GJumpSubtype::cjmp_flt_ueq:
            case GJumpSubtype::cjmp_flt_ugt:
            case GJumpSubtype::cjmp_flt_uge:
            case GJumpSubtype::cjmp_flt_ult:
            case GJumpSubtype::cjmp_flt_ule:
            case GJumpSubtype::cjmp_flt_une:
            case GJumpSubtype::fcmp_oeq:
            case GJumpSubtype::fcmp_ogt:
            case GJumpSubtype::fcmp_oge:
            case GJumpSubtype::fcmp_olt:
            case GJumpSubtype::fcmp_ole:
            case GJumpSubtype::fcmp_one:
            case GJumpSubtype::fcmp_ord:
            case GJumpSubtype::fcmp_uno:
            case GJumpSubtype::fcmp_ueq:
            case GJumpSubtype::fcmp_ugt:
            case GJumpSubtype::fcmp_uge:
            case GJumpSubtype::fcmp_ult:
            case GJumpSubtype::fcmp_ule:
            case GJumpSubtype::fcmp_une:
              if (legalize_fcmp(bb, i)) {
                ioff = 0;
              }
              break;
            default:
              break;
          }
          break;
        case GOpcode::GConv:
          switch ((GConvSubtype)bb.instrs[i].sop) {
            case GConvSubtype::mov_zx:
            case GConvSubtype::mov_sx:
              if (legalize_move(bb, i)) {
                ioff = 0;
              }
              break;
            case GConvSubtype::UI2FL:
            case GConvSubtype::FL2UI:
            case GConvSubtype::FL2SI:
            case GConvSubtype::SI2FL:
              if (legalize_conversion(bb, i)) {
                ioff = 0;
              }
              break;
            default:
              break;
          }
          break;
        case GOpcode::GArith:
          switch ((GArithSubtype)bb.instrs[i].sop) {
            case GArithSubtype::land2:
            case GArithSubtype::lor2:
              if (legalize_arith_op(bb, i)) {
                ioff = 0;
              }
              break;
            case GArithSubtype::udiv:
            case GArithSubtype::idiv:
              if (legalize_idiv(bb, i)) {
                ioff = 0;
              }
              break;
            default:
              break;
          }
          break;
        case GOpcode::GCMov:
          switch ((GCMovSubtype)bb.instrs[i].sop) {
            case GCMovSubtype::cmov_sgt:
            case GCMovSubtype::cmov_slt:
            case GCMovSubtype::cmov_ult:
            case GCMovSubtype::cmov_sge:
            case GCMovSubtype::cmov_sle:
            case GCMovSubtype::cmov_ne:
            case GCMovSubtype::cmov_eq:
            case GCMovSubtype::cmov_ugt:
            case GCMovSubtype::cmov_uge:
            case GCMovSubtype::cmov_ule:
              if (legalize_cmoveXX(bb, i)) {
                ioff = 0;
              }
              break;
            case GCMovSubtype::cmov:
              if (legalize_cmove(bb, i)) {
                ioff = 0;
              }
              break;
            default:
              break;
          }
          break;
        case GOpcode::GVec:
          switch ((GVecSubtype)bb.instrs[i].sop) {
            case GVecSubtype::fmul:
            case GVecSubtype::fdiv:
            case GVecSubtype::vsub:
            case GVecSubtype::fxor:
            case GVecSubtype::vadd:
            case GVecSubtype::fAnd:
            case GVecSubtype::fOr:
              if (legalize_floating_binary_ops(bb, i)) {
                ioff = 0;
              }
              break;
            default:
              break;
          }
          break;
        case GOpcode::X86:
          switch ((X86Subtype)bb.instrs[i].sop) {
            case X86Subtype::punpckl:
              if (legalize_punpckl(bb, i)) {
                ioff = 0;
              }
              break;
            default:
              break;
          }
          break;
      }
    }
  }
}

void Legalizer::apply(MFunc &func) { apply_impl(func); }

void Legalizer::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("MIR Legalizer");
  for (auto &func : funcs) {
    apply(func);
  }
}

}  // namespace foptim::fmir
