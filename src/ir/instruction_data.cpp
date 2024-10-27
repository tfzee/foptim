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
  case InstrType::ICmp:
  case InstrType::SExt:
  case InstrType::ZExt:
    return true;
  case InstrType::DirectCallInstr:
    return !this->get_type()->is_void();
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
    return false;
    break;
  }
}

bool InstrData::is_critical() const {
  switch (instr_type) {
  case InstrType::DirectCallInstr:
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
    return true;
  case InstrType::AllocaInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::ICmp:
  case InstrType::SExt:
  case InstrType::ZExt:
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
      return true;
    case BinaryInstrSubType::IntSRem:
      return false;
    }
  case InstrType::ICmp:
    switch ((ICmpInstrSubType)subtype) {
    case ICmpInstrSubType::INVALID:
    case ICmpInstrSubType::NE:
    case ICmpInstrSubType::EQ:
      return true;
    case ICmpInstrSubType::SLT:
    case ICmpInstrSubType::ULT:
    case ICmpInstrSubType::SGT:
    case ICmpInstrSubType::UGT:
    case ICmpInstrSubType::UGE:
    case ICmpInstrSubType::ULE:
    case ICmpInstrSubType::SGE:
    case ICmpInstrSubType::SLE:
      return false;
    }
  case InstrType::DirectCallInstr:
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
  case InstrType::AllocaInstr:
  case InstrType::LoadInstr:
  case InstrType::SExt:
  case InstrType::ZExt:
    return false;
  }
}

bool InstrData::pot_modifies_mem() const {
  switch (instr_type) {
  case InstrType::DirectCallInstr:
  case InstrType::StoreInstr:
    return true;
  case InstrType::AllocaInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::ReturnInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::ICmp:
  case InstrType::SExt:
  case InstrType::ZExt:
    return false;
  }
}
bool InstrData::has_pot_sideeffects() const {
  switch (instr_type) {
  case InstrType::DirectCallInstr:
  case InstrType::StoreInstr:
  case InstrType::AllocaInstr:
    return true;
  case InstrType::ReturnInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::ICmp:
  case InstrType::SExt:
  case InstrType::ZExt:
    return false;
  }
}

InstrData InstrData::get_direct_call(TypeR ty) {
  auto res = InstrData{InstrType::DirectCallInstr, ty,
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

InstrData InstrData::get_mul(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::IntMul,
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
