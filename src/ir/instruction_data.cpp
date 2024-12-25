#include "instruction_data.hpp"
#include "context.hpp"
#include "ir/basic_block_ref.hpp"
#include "utils/logging.hpp"
// #include <algorithm>

namespace foptim::fir {

bool InstrData::verify(const BasicBlockData *exp_parent,
                       utils::Printer printer) const {
  for (const auto &use : uses) {
    if (!use.user.is_valid()) {
      return false;
    }
  }
  for (const auto &[bb, bb_args] : bbs) {
    if (!bb.is_valid()) {
      printer << "Instr references a invalid bb\n";
      return false;
    }
    if (bb->get_parent() != exp_parent->get_parent()) {
      printer << "Instr references a bb thats not part of this function\n";
      return false;
    }
    if (bb_args.size() != bb->n_args()) {
      printer << "Instr has invalid number of basicblock arguments\n";
      printer << "Expected: " << bb->n_args() << " Got:" << bb_args.size()
              << "\n";
      return false;
    }
    for (const auto &bb_arg : bb_args) {
      if (!bb_arg.is_valid(true)) {
        printer << "Instr references a value that is not valid\n";
        printer << "Value: " << bb_arg << "\n";
        return false;
      }
      if (!bb_arg.is_constant() && bb_arg.get_n_uses() == 0) {
        printer << "BB Arg does have 0 uses but its used here\n";
        printer << "In BBArg: " << bb_arg << "\n";
        return false;
      }
    }
  }

  for (const auto &arg : args) {
    if (!arg.is_constant()) {
      if (arg.get_n_uses() == 0) {
        printer << "Arg does have 0 uses but its used here\n";
        printer << "In Arg: " << arg << "\n";
        return false;
      }

      bool found = false;
      for (const auto &arg_use : *arg.get_uses()) {
        if (arg_use.user.get_raw_ptr() == this) {
          found = true;
        }
      }
      if (!found) {
        printer << "Arg does not have this instruction as user\n";
        printer << "In Arg: " << arg << "\n";
        return false;
      }
    }

    if (arg.is_instr() &&
        (!arg.as_instr().is_valid() || !arg.as_instr()->parent.is_valid())) {
      printer << "Got invalid instruction as argument\n";
      return false;
    }
  }
  if (!parent.is_valid() || parent.get_raw_ptr() != exp_parent) {
    printer << "Instructions parent does not match with basic block it is in\n";
    return false;
  }
  return true;
}

bool InstrData::has_result() const {
  switch (instr_type) {
  case InstrType::BinaryInstr:
  case InstrType::AllocaInstr:
  case InstrType::LoadInstr:
  case InstrType::SelectInstr:
  case InstrType::ICmp:
  case InstrType::FCmp:
  case InstrType::ITrunc:
  case InstrType::SExt:
  case InstrType::ZExt:
  case InstrType::Conversion:
    return true;
  case InstrType::CallInstr:
    return !this->get_type()->is_void();
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
    return false;
  }
}

bool InstrData::is_critical() const {
  switch (instr_type) {
  case InstrType::CallInstr:
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
    return true;
  case InstrType::AllocaInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::SelectInstr:
  case InstrType::ITrunc:
  case InstrType::ICmp:
  case InstrType::FCmp:
  case InstrType::SExt:
  case InstrType::ZExt:
  case InstrType::Conversion:
    return false;
  }
}

bool InstrData::is_commutative() const {
  switch (instr_type) {
  case InstrType::BinaryInstr:
    switch ((BinaryInstrSubType)subtype) {
    case BinaryInstrSubType::INVALID:
    case BinaryInstrSubType::IntAdd:
    case BinaryInstrSubType::IntMul:
    case BinaryInstrSubType::Or:
    case BinaryInstrSubType::Xor:
    case BinaryInstrSubType::And:
      return true;
    case BinaryInstrSubType::IntSub:
    case BinaryInstrSubType::IntSRem:
    case BinaryInstrSubType::IntSDiv:
    case BinaryInstrSubType::IntUDiv:
    case BinaryInstrSubType::FloatAdd:
    case BinaryInstrSubType::FloatSub:
    case BinaryInstrSubType::FloatMul:
    case BinaryInstrSubType::FloatDiv:
    case BinaryInstrSubType::Shl:
    case BinaryInstrSubType::Shr:
    case BinaryInstrSubType::AShr:
      return false;
    }
  case InstrType::FCmp:
    switch ((FCmpInstrSubType)subtype) {
    case FCmpInstrSubType::INVALID:
    case FCmpInstrSubType::AlwFalse:
    case FCmpInstrSubType::ORD:
    case FCmpInstrSubType::UNO:
    case FCmpInstrSubType::OEQ:
    case FCmpInstrSubType::ONE:
    case FCmpInstrSubType::UEQ:
    case FCmpInstrSubType::UNE:
    case FCmpInstrSubType::AlwTrue:
      return true;
    default:
      return false;
    }
  case InstrType::ICmp:
    switch ((ICmpInstrSubType)subtype) {
    case ICmpInstrSubType::INVALID:
    case ICmpInstrSubType::NE:
    case ICmpInstrSubType::EQ:
      return true;
    default:
      return false;
    }
  case InstrType::CallInstr:
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::SelectInstr:
  case InstrType::StoreInstr:
  case InstrType::AllocaInstr:
  case InstrType::LoadInstr:
  case InstrType::SExt:
  case InstrType::ZExt:
  case InstrType::ITrunc:
  case InstrType::Conversion:
    return false;
  }
  UNREACH();
}

bool InstrData::pot_modifies_mem() const {
  switch (instr_type) {
  case InstrType::CallInstr:
  case InstrType::StoreInstr:
    return true;
  case InstrType::AllocaInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::SelectInstr:
  case InstrType::ReturnInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::ICmp:
  case InstrType::FCmp:
  case InstrType::SExt:
  case InstrType::ZExt:
  case InstrType::ITrunc:
  case InstrType::Conversion:
    return false;
  }
}
bool InstrData::has_pot_sideeffects() const {
  switch (instr_type) {
  case InstrType::CallInstr:
  case InstrType::StoreInstr:
  case InstrType::AllocaInstr:
    return true;
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::SelectInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::ICmp:
  case InstrType::FCmp:
  case InstrType::SExt:
  case InstrType::ZExt:
  case InstrType::ITrunc:
  case InstrType::Conversion:
    return false;
  }
}

InstrData InstrData::get_call(TypeR ty) {
  auto res =
      InstrData{InstrType::CallInstr, ty, BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_float_add(TypeR ty) {
  auto res =
      InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::FloatAdd, ty,
                BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_float_sub(TypeR ty) {
  auto res =
      InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::FloatSub, ty,
                BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_float_mul(TypeR ty) {
  auto res =
      InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::FloatMul, ty,
                BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_add(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::IntAdd,
                       ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_smod(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::IntSRem,
                       ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_binary(TypeR ty, BinaryInstrSubType sub_type) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)sub_type, ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_conversion(TypeR ty, ConversionSubType sub_type) {
  auto res = InstrData{InstrType::Conversion, (u32)sub_type, ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_mul(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::IntMul,
                       ty, BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_shl(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::Shl, ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_ashr(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::AShr,
                       ty, BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_lshr(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::Shr, ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_sub(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::IntSub,
                       ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_sext(TypeR ty) {
  auto res =
      InstrData{InstrType::SExt, 0, ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  return res;
}

InstrData InstrData::get_itrunc(TypeR ty) {
  auto res =
      InstrData{InstrType::ITrunc, 0, ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  return res;
}

InstrData InstrData::get_zext(TypeR ty) {
  auto res =
      InstrData{InstrType::ZExt, 0, ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  return res;
}

InstrData InstrData::get_int_cmp(TypeR ty, ICmpInstrSubType cmp_ty) {
  auto res = InstrData{InstrType::ICmp, (u32)cmp_ty, ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_float_cmp(TypeR ty, FCmpInstrSubType cmp_ty) {
  auto res = InstrData{InstrType::FCmp, (u32)cmp_ty, ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_return(TypeR ty) {
  auto res =
      InstrData{InstrType::ReturnInstr, ty, BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_alloca(TypeR ty) {
  auto res =
      InstrData{InstrType::AllocaInstr, ty, BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_load(TypeR ty) {
  auto res =
      InstrData{InstrType::LoadInstr, ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  return res;
}

InstrData InstrData::get_select(TypeR ty) {
  auto res =
      InstrData{InstrType::SelectInstr, ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  return res;
}

InstrData InstrData::get_store(TypeR ty) {
  auto res =
      InstrData{InstrType::StoreInstr, ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_branch(ContextData *ctx) {
  auto res = InstrData{InstrType::BranchInstr, ctx->get_void_type(),
                       BasicBlock(BasicBlock::invalid())};
  // res.bbs.reserve(1);
  return res;
}

InstrData InstrData::get_cond_branch(ContextData *ctx) {
  auto res = InstrData{InstrType::CondBranchInstr, ctx->get_void_type(),
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  // res.bbs.reserve(2);
  return res;
}

} // namespace foptim::fir
