#include "instruction_data.hpp"
#include "context.hpp"
#include "ir/basic_block_ref.hpp"

namespace foptim::fir {

bool InstrData::verify(const BasicBlockData *exp_parent) const {
  for (const auto &use : uses) {
    if (!use.user.is_valid()) {
      return false;
    }
  }
  size_t arg_id = 0;
  for (const auto &[bb, bb_args] : bbs) {
    if (!bb.is_valid()) {
      fmt::print("Instr references a invalid bb\n");
      return false;
    }
    if (bb->get_parent() != exp_parent->get_parent()) {
      fmt::print("Instr references a bb thats not part of this function\n");
      return false;
    }
    if (bb_args.size() != bb->n_args()) {
      fmt::print("Instr has invalid number of basicblock arguments\n");
      fmt::println("Expected: {} Got: {}", bb->n_args(), bb_args.size());
      return false;
    }

    size_t bb_arg_id = 0;
    for (const auto &bb_arg : bb_args) {
      if (!bb_arg.is_valid(true)) {
        fmt::print("Instr references a value that is not valid\n");
        fmt::print("Value: {}\n", bb_arg);
        return false;
      }
      if (!bb_arg.is_constant()) {
        if (bb_arg.get_n_uses() == 0) {
          fmt::print("BB Arg does have 0 uses but its used here\n");
          fmt::print("In BBArg: {}\n", bb_arg);
          return false;
        }
        bool found = false;
        for (const auto &arg_use : *bb_arg.get_uses()) {
          if (arg_use.user.get_raw_ptr() == this && arg_use.argId == arg_id &&
              arg_use.bbArgId == bb_arg_id) {
            found = true;
          }
        }
        if (!found) {
          fmt::print("Arg does not have this instruction as user\n");
          fmt::print("In Arg: {}\n", bb_arg);
          return false;
        }
      }
      bb_arg_id++;
    }
    arg_id++;
  }

  arg_id = 0;
  for (const auto &arg : args) {
    if (!arg.is_valid(true)) {
      fmt::print("Got invalid argument\n");
      return false;
    }
    if (!arg.is_constant()) {
      if (arg.get_n_uses() == 0) {
        fmt::print("Arg does have 0 uses but its used here\n");
        fmt::print("In Arg: {}\n", arg);
        return false;
      }

      bool found = false;
      for (const auto &arg_use : *arg.get_uses()) {
        if (arg_use.user.get_raw_ptr() == this && arg_use.argId == arg_id) {
          found = true;
        }
      }
      if (!found) {
        fmt::print("Arg does not have this instruction as user\n");
        fmt::print("In Arg: {}\n", arg);
        return false;
      }
    }

    if (arg.is_instr() &&
        (!arg.as_instr().is_valid() || !arg.as_instr()->parent.is_valid())) {
      fmt::print("Got invalid instruction as argument\n");
      return false;
    }
    arg_id++;
  }
  if (is(InstrType::CallInstr) && args[0].is_constant() &&
      args[0].as_constant()->is_func()) {
    auto funcy = args[0].as_constant()->as_func();
    if (!funcy.func->variadic &&
        funcy.func->func_ty->as_func().arg_types.size() + 1 != args.size()) {
      fmt::print("Call instr has wrong number of arguments\n");
      return false;
    }
  }
  if (!parent.is_valid() || parent.get_raw_ptr() != exp_parent) {
    fmt::print(
        "Instructions parent does not match with basic block it is in\n");
    return false;
  }
  return true;
}

bool InstrData::has_result() const {
  switch (instr_type) {
  case InstrType::BinaryInstr:
  case InstrType::ExtractValue:
  case InstrType::UnaryInstr:
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
  case InstrType::InsertValue:
  case InstrType::ReturnInstr:
  case InstrType::Unreachable:
  case InstrType::BranchInstr:
  case InstrType::SwitchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
    return false;
  case InstrType::Intrinsic:
    return !this->get_type()->is_void();
  }
}

bool InstrData::is_critical() const {
  switch (instr_type) {
  case InstrType::ReturnInstr:
  case InstrType::Unreachable:
  case InstrType::BranchInstr:
  case InstrType::SwitchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
    return true;
  case InstrType::CallInstr:
    if (args[0].is_constant() && args[0].as_constant()->is_func()) {
      return !args[0].as_constant()->as_func()->mem_read_none;
    }
    return true;
  case InstrType::Intrinsic:
    switch ((IntrinsicSubType)subtype) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTLZ:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
      return true;
    }
  case InstrType::InsertValue:
  case InstrType::ExtractValue:
  case InstrType::AllocaInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::UnaryInstr:
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
  case InstrType::Intrinsic:
    switch ((IntrinsicSubType)subtype) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTLZ:
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
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
  case InstrType::UnaryInstr:
  case InstrType::CallInstr:
  case InstrType::ReturnInstr:
  case InstrType::Unreachable:
  case InstrType::BranchInstr:
  case InstrType::SwitchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::SelectInstr:
  case InstrType::StoreInstr:
  case InstrType::AllocaInstr:
  case InstrType::LoadInstr:
  case InstrType::SExt:
  case InstrType::ZExt:
  case InstrType::ITrunc:
  case InstrType::Conversion:
  case InstrType::InsertValue:
  case InstrType::ExtractValue:
    return false;
  }
  UNREACH();
}

bool InstrData::pot_modifies_mem() const {
  switch (instr_type) {
  case InstrType::CallInstr:
    if (args[0].is_constant() && args[0].as_constant()->is_func()) {
      auto f = args[0].as_constant()->as_func();
      return !f->mem_read_none && !f->mem_read_only;
    }
    return true;
  case InstrType::StoreInstr:
    return true;
  case InstrType::Intrinsic:
    switch ((IntrinsicSubType)subtype) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTLZ:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
      return true;
    }
  case InstrType::InsertValue:
  case InstrType::ExtractValue:
  case InstrType::AllocaInstr:
  case InstrType::BranchInstr:
  case InstrType::SwitchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::SelectInstr:
  case InstrType::ReturnInstr:
  case InstrType::Unreachable:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::UnaryInstr:
  case InstrType::ICmp:
  case InstrType::FCmp:
  case InstrType::SExt:
  case InstrType::ZExt:
  case InstrType::ITrunc:
  case InstrType::Conversion:
    return false;
  }
}

bool InstrData::pot_reads_mem() const {
  switch (instr_type) {
  case InstrType::CallInstr:
    if (args[0].is_constant() && args[0].as_constant()->is_func()) {
      auto f = args[0].as_constant()->as_func();
      return !f->mem_read_none;
    }
    return true;
  case InstrType::Intrinsic:
    switch ((IntrinsicSubType)subtype) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTLZ:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
      return true;
    }
  case InstrType::LoadInstr:
    return true;
  case InstrType::StoreInstr:
  case InstrType::InsertValue:
  case InstrType::ExtractValue:
  case InstrType::AllocaInstr:
  case InstrType::BranchInstr:
  case InstrType::SwitchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::SelectInstr:
  case InstrType::ReturnInstr:
  case InstrType::Unreachable:
  case InstrType::BinaryInstr:
  case InstrType::UnaryInstr:
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
  case InstrType::Intrinsic:
    switch ((IntrinsicSubType)subtype) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTLZ:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
      return true;
    }
  case InstrType::InsertValue:
  case InstrType::ExtractValue:
  case InstrType::ReturnInstr:
  case InstrType::Unreachable:
  case InstrType::SwitchInstr:
  case InstrType::BranchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::SelectInstr:
  case InstrType::LoadInstr:
  case InstrType::BinaryInstr:
  case InstrType::UnaryInstr:
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

InstrData InstrData::get_float_div(TypeR ty) {
  auto res =
      InstrData{InstrType::BinaryInstr, (u32)BinaryInstrSubType::FloatDiv, ty,
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

InstrData InstrData::get_intrinsic(TypeR ty, IntrinsicSubType sub_type) {
  auto res = InstrData{InstrType::Intrinsic, (u32)sub_type, ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_binary(TypeR ty, BinaryInstrSubType sub_type) {
  auto res = InstrData{InstrType::BinaryInstr, (u32)sub_type, ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_unary(TypeR ty, UnaryInstrSubType sub_type) {
  auto res = InstrData{InstrType::UnaryInstr, (u32)sub_type, ty,
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
  return res;
}

InstrData InstrData::get_extract_value(TypeR ty) {
  auto res = InstrData{InstrType::ExtractValue, 0, ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_insert_value(TypeR ty) {
  auto res = InstrData{InstrType::InsertValue, 0, ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_unreach(TypeR ty) {
  auto res =
      InstrData{InstrType::Unreachable, ty, BasicBlock(BasicBlock::invalid())};
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

InstrData InstrData::get_switch(ContextData *ctx) {
  auto res = InstrData{InstrType::SwitchInstr, ctx->get_void_type(),
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
