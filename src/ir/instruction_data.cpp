#include "instruction_data.hpp"

#include "context.hpp"
#include "ir/basic_block_ref.hpp"

namespace foptim::fir {

bool BBRefWithArgs::operator==(const BBRefWithArgs &other) const {
  if (bb != other.bb || args.size() != other.args.size()) {
    return false;
  }
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i].eql(other.args[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool InstrData::eql_expr(const InstrData &other) const {
  // TODO: compare on type?? value_type.eql(other.value_type)
  if (instr_type != other.instr_type) {
    return false;
  }
  if (subtype != other.subtype) {
    return false;
  }
  // TODO: attributes
  if (args.size() != other.args.size() || bbs.size() != other.bbs.size()) {
    return false;
  }
  for (size_t i = 0; i < args.size(); i++) {
    if (!args[i].eql(other.args[i])) {
      return false;
    }
  }
  for (size_t i = 0; i < bbs.size(); i++) {
    if (bbs[i] != other.bbs[i]) {
      return false;
    }
    for (size_t j = 0; j < bbs[i].args.size(); j++) {
      if (!bbs[i].args[j].eql(other.bbs[i].args[j])) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] const char *InstrData::get_name() const {
  switch (instr_type) {
  case InstrType::Fence:
    return "Fence";
  case InstrType::AtomicRMW:
    switch (static_cast<AtomicRMWSubType>(subtype)) {
    case AtomicRMWSubType::INVALID:
      return "ATOMICRMW_INVALID";
    case AtomicRMWSubType::Add:
      return "AtomicRMW.Add";
    case AtomicRMWSubType::Xchg:
      return "AtomicRMW.Xchg";
    case AtomicRMWSubType::Or:
      return "AtomicRMW.Or";
    }
  case InstrType::VectorInstr:
    switch (static_cast<VectorISubType>(subtype)) {
    case VectorISubType::INVALID:
      return "VECTORINSTR_INVALID";
    case VectorISubType::Broadcast:
      return "V.Broadcast";
    case VectorISubType::HorizontalAdd:
      return "V.HAdd";
    case VectorISubType::Concat:
      return "V.Concat";
    case VectorISubType::ExtractHigh:
      return "V.ExtractHigh";
    case VectorISubType::ExtractLow:
      return "V.ExtractLow";
    case VectorISubType::HorizontalMul:
      return "V.HMul";
    case VectorISubType::Shuffle:
      return "V.Shuffle";
    }
  case InstrType::Intrinsic:
    switch (static_cast<IntrinsicSubType>(subtype)) {
    case IntrinsicSubType::INVALID:
      return "INTRINSIC_INVALID";
    case IntrinsicSubType::CTTZ:
      return "INTRIN:CTTZ";
    case IntrinsicSubType::CTLZ:
      return "INTRIN:CTLZ";
    case IntrinsicSubType::VA_start:
      return "INTRIN:VA_START";
    case IntrinsicSubType::Abs:
      return "INTRIN:ABS";
    case IntrinsicSubType::FAbs:
      return "INTRIN:FABS";
    case IntrinsicSubType::VA_end:
      return "INTRIN:VA_END";
    case IntrinsicSubType::UMin:
      return "INTRIN:UMin";
    case IntrinsicSubType::UMax:
      return "INTRIN:UMax";
    case IntrinsicSubType::SMin:
      return "INTRIN:SMin";
    case IntrinsicSubType::SMax:
      return "INTRIN:SMax";
    case IntrinsicSubType::FMin:
      return "INTRIN:FMin";
    case IntrinsicSubType::FMax:
      return "INTRIN:FMax";
    case IntrinsicSubType::FMinimum:
      return "INTRIN:FMinimum";
    case IntrinsicSubType::FMaximum:
      return "INTRIN:FMaximum";
    case IntrinsicSubType::FRound:
      return "INTRIN:FRound";
    case IntrinsicSubType::FFloor:
      return "INTRIN:FFloor";
    case IntrinsicSubType::FCeil:
      return "INTRIN:FCeil";
    case IntrinsicSubType::FTrunc:
      return "INTRIN:FTrunc";
    case IntrinsicSubType::IsConstant:
      return "INTRIN:IsConstant";
    case IntrinsicSubType::PopCnt:
      return "INTRIN:PopCnt";
    case IntrinsicSubType::Memcpy:
      return "INTRIN:MemCpy";
    case IntrinsicSubType::Memset:
      return "INTRIN:MemSet";
    }
  case InstrType::Unreachable:
    return "unreachable";
  case InstrType::UnaryInstr:
    switch (static_cast<UnaryInstrSubType>(subtype)) {
    case UnaryInstrSubType::INVALID:
      return "UNARYYOP_INVALID";
    case UnaryInstrSubType::FloatSqrt:
      return "FloatSqrt";
    case UnaryInstrSubType::FloatNeg:
      return "FloatNeg";
    case UnaryInstrSubType::IntNeg:
      return "IntNeg";
    case UnaryInstrSubType::Not:
      return "Not";
    }
  case InstrType::BinaryInstr:
    switch (static_cast<BinaryInstrSubType>(subtype)) {
    case BinaryInstrSubType::INVALID:
      return "BINARYOP_INVALID";
    case BinaryInstrSubType::PtrAdd:
      return "PtrAdd";
    case BinaryInstrSubType::IntAdd:
      return "IntAdd";
    case BinaryInstrSubType::IntSub:
      return "IntSub";
    case BinaryInstrSubType::IntMul:
      return "IntMul";
    case BinaryInstrSubType::IntSRem:
      return "IntSRem";
    case BinaryInstrSubType::IntURem:
      return "IntURem";
    case BinaryInstrSubType::IntSDiv:
      return "IntSDiv";
    case BinaryInstrSubType::IntUDiv:
      return "IntUDiv";
    case BinaryInstrSubType::FloatAdd:
      return "FloatAdd";
    case BinaryInstrSubType::FloatSub:
      return "FloatSub";
    case BinaryInstrSubType::FloatMul:
      return "FloatMul";
    case BinaryInstrSubType::FloatDiv:
      return "FloatDiv";
    case BinaryInstrSubType::Shl:
      return "Shl";
    case BinaryInstrSubType::Shr:
      return "LShR";
    case BinaryInstrSubType::AShr:
      return "AShR";
    case BinaryInstrSubType::Or:
      return "Or";
    case BinaryInstrSubType::Xor:
      return "Xor";
    case BinaryInstrSubType::And:
      return "And";
    }
  case InstrType::Conversion:
    switch (static_cast<ConversionSubType>(subtype)) {
    case ConversionSubType::INVALID:
      return "CONVERSIONOP_INVALID";
    case ConversionSubType::FPTOUI:
      return "FP_UI";
    case ConversionSubType::BitCast:
      return "BITCAST";
    case ConversionSubType::FPTOSI:
      return "FP_SI";
    case ConversionSubType::UITOFP:
      return "UI_FP";
    case ConversionSubType::SITOFP:
      return "SI_FP";
    case ConversionSubType::IntToPtr:
      return "INT_PTR";
    case ConversionSubType::PtrToInt:
      return "PTR_INT";
    case ConversionSubType::FPEXT:
      return "FP_EXT";
    case ConversionSubType::FPTRUNC:
      return "FP_TRUNC";
    }
  case InstrType::InsertValue:
    return "InsertValue";
  case InstrType::ExtractValue:
    return "ExtractValue";
  case InstrType::ITrunc:
    return "ITrunc";
  case InstrType::SExt:
    return "SExt";
  case InstrType::SelectInstr:
    return "Select";
  case InstrType::ZExt:
    return "ZExt";
  case InstrType::CondBranchInstr:
    return "CondBranch";
  case InstrType::SwitchInstr:
    return "Switch";
  case InstrType::CallInstr:
    return "Call";
  case InstrType::LoadInstr:
    return "Load";
  case InstrType::StoreInstr:
    return "Store";
  case InstrType::AllocaInstr:
    return "Alloca";
  case InstrType::ReturnInstr:
    return "Return";
  case InstrType::BranchInstr:
    return "Branch";
  case InstrType::ICmp:
    switch (static_cast<ICmpInstrSubType>(subtype)) {
    case ICmpInstrSubType::INVALID:
      return "INVALID";
    case ICmpInstrSubType::ULT:
      return "IntULT";
    case ICmpInstrSubType::SLT:
      return "IntSLT";
    case ICmpInstrSubType::NE:
      return "IntNE";
    case ICmpInstrSubType::EQ:
      return "IntEQ";
    case ICmpInstrSubType::SGT:
      return "IntSGT";
    case ICmpInstrSubType::UGT:
      return "IntUGT";
    case ICmpInstrSubType::UGE:
      return "IntUGE";
    case ICmpInstrSubType::ULE:
      return "IntULE";
    case ICmpInstrSubType::SGE:
      return "IntSGE";
    case ICmpInstrSubType::SLE:
      return "IntSLE";
    case ICmpInstrSubType::MulOverflow:
      return "IntMulOverflow";
    case ICmpInstrSubType::AddOverflow:
      return "IntAddOverflow";
    }
  case InstrType::FCmp:
    switch (static_cast<FCmpInstrSubType>(subtype)) {
    case FCmpInstrSubType::INVALID:
      return "FloatINVALID";
    case FCmpInstrSubType::IsNaN:
      return "FloatIsNaN";
    case FCmpInstrSubType::AlwFalse:
      return "FloatAlwFalse";
    case FCmpInstrSubType::OEQ:
      return "FloatOEQ";
    case FCmpInstrSubType::OGT:
      return "FloatOGT";
    case FCmpInstrSubType::OGE:
      return "FloatOGE";
    case FCmpInstrSubType::OLT:
      return "FloatOLT";
    case FCmpInstrSubType::OLE:
      return "FloatOLE";
    case FCmpInstrSubType::ONE:
      return "FloatONE";
    case FCmpInstrSubType::ORD:
      return "FloatORD";
    case FCmpInstrSubType::UNO:
      return "FloatUNO";
    case FCmpInstrSubType::UEQ:
      return "FloatUEQ";
    case FCmpInstrSubType::UGT:
      return "FloatUGT";
    case FCmpInstrSubType::UGE:
      return "FloatUGE";
    case FCmpInstrSubType::ULT:
      return "FloatULT";
    case FCmpInstrSubType::ULE:
      return "FloatULE";
    case FCmpInstrSubType::UNE:
      return "FloatUNE";
    case FCmpInstrSubType::AlwTrue:
      return "FloatAlwTrue";
    }
  }
  ASSERT_M(false, "Add your instruction to the get_name function");
  return "UNKNOWN";
}

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
    auto bb_found =
        std::ranges::find(exp_parent->get_parent()->basic_blocks, bb);
    if (exp_parent->get_parent()->basic_blocks.end() == bb_found) {
      fmt::print("BB is reference and claims to have same parent but not found "
                 "in function\n");
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

    if (arg.is_instr()) {
      auto arg_i = arg.as_instr();
      if (arg_i.get_raw_ptr() == this) {
        fmt::print("Cant have a isntruction referencing itself\n");
        return false;
      }
      if (!arg_i.is_valid() || !arg.as_instr()->parent.is_valid()) {
        fmt::print("Got invalid instruction as argument\n");
        return false;
      }
    }
    arg_id++;
  }
  if (is(InstrType::CallInstr) && args[0].is_constant() &&
      args[0].as_constant()->is_func()) {
    auto funcy = args[0].as_constant()->as_func();
    if (!funcy.func->attribs.variadic &&
        funcy.func->func_ty->as_func().arg_types.size() + 1 != args.size()) {
      fmt::print("Call instr has wrong number of arguments\n");
      return false;
    }
  }
  if (is(InstrType::StoreInstr)) {
    if (get_type()->get_bitwidth() != args[1].get_type()->get_bitwidth()) {
      fmt::print("Trying to store value with wrong size\n");
      return false;
    }
  }
  if (is(BinaryInstrSubType::FloatAdd) || is(BinaryInstrSubType::FloatDiv) ||
      is(BinaryInstrSubType::FloatMul) || is(BinaryInstrSubType::FloatSub)) {
    auto t1 = args[0].get_type();
    auto t2 = args[1].get_type();
    if ((!t1->is_float() && !t1->is_vec()) ||
        (!t2->is_float() && !t2->is_vec())) {
      fmt::print("Float operation with non float type\n");
      return false;
    }
  }
  if (is(BinaryInstrSubType::And) || is(BinaryInstrSubType::Or) ||
      is(BinaryInstrSubType::Xor)) {
    auto t0 = get_type();
    auto t1 = args[0].get_type();
    auto t2 = args[1].get_type();
    bool type_missmatch =
        !(t0->is_float() && t1->is_float() && t2->is_float()) &&
        !((t0->is_ptr() || t0->is_int()) && (t1->is_ptr() || t1->is_int()) &&
          (t2->is_ptr() || t2->is_int())) &&
        !(t0->is_vec() && t1->is_vec() && t2->is_vec());
    if (t1->get_bitwidth() != t2->get_bitwidth() || type_missmatch) {
      fmt::println("{} {} {}", t0, t1, t2);
      fmt::print("And/Or/Xor operation type missmatch\n");
      return false;
    }
  }
  if (is(ConversionSubType::BitCast)) {
    if (get_type()->get_bitwidth() != args[0].get_type()->get_bitwidth()) {
      fmt::print("Trying to bitcast value with wrong size {} != {}\n",
                 get_type()->get_bitwidth(),
                 args[0].get_type()->get_bitwidth());
      return false;
    }
  }
  if (is(InstrType::ExtractValue)) {
    if (!args[0].get_type()->is_struct() && !args[0].get_type()->is_vec()) {
      fmt::print("ExtractValue only works on a vec/struct argument not on {}\n",
                 args[0].get_type());
      return false;
    }
    if (args[0].get_type()->is_vec() &&
        args[0].get_type()->as_vec().member_number <=
            args[1].as_constant()->as_int()) {
      fmt::print(
          "ExtractValue index gotta be smaller then size of the input size({}) "
          "<= {}\n",
          args[0], args[1]);
      return false;
    }
  }
  if (is(BinaryInstrSubType::IntAdd)) {
    if (get_type()->get_bitwidth() < args[0].get_type()->get_bitwidth() ||
        get_type()->get_bitwidth() < args[1].get_type()->get_bitwidth()) {
      fmt::print(
          "Result of add cant be smaller then the inputs {:c} vs {:c} + {:c}\n",
          get_type(), args[0].get_type(), args[1].get_type());
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
  case InstrType::VectorInstr:
  case InstrType::AtomicRMW:
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
  case InstrType::Fence:
    return false;
  case InstrType::Intrinsic:
    return !this->get_type()->is_void();
  }
}

bool InstrData::is_critical() const {
  // a volatile load is observable even if its result is unused
  if (instr_type == InstrType::LoadInstr && Volatile) {
    return true;
  }
  switch (instr_type) {
  case InstrType::ReturnInstr:
  case InstrType::Unreachable:
  case InstrType::BranchInstr:
  case InstrType::SwitchInstr:
  case InstrType::CondBranchInstr:
  case InstrType::StoreInstr:
  case InstrType::AtomicRMW:
  case InstrType::Fence:
    return true;
  case InstrType::CallInstr:
    if (args[0].is_constant() && args[0].as_constant()->is_func()) {
      return !args[0].as_constant()->as_func()->attribs.mem_read_none;
    }
    return true;
  case InstrType::Intrinsic:
    switch (static_cast<IntrinsicSubType>(subtype)) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTLZ:
    case IntrinsicSubType::Abs:
    case IntrinsicSubType::FAbs:
    case IntrinsicSubType::UMin:
    case IntrinsicSubType::UMax:
    case IntrinsicSubType::SMin:
    case IntrinsicSubType::SMax:
    case IntrinsicSubType::FMin:
    case IntrinsicSubType::FMax:
    case IntrinsicSubType::FMinimum:
    case IntrinsicSubType::FMaximum:
    case IntrinsicSubType::IsConstant:
    case IntrinsicSubType::PopCnt:
    case IntrinsicSubType::FRound:
    case IntrinsicSubType::FFloor:
    case IntrinsicSubType::FCeil:
    case IntrinsicSubType::FTrunc:
    case IntrinsicSubType::CTTZ:
    case fir::IntrinsicSubType::Memset:
    case fir::IntrinsicSubType::Memcpy:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
      return true;
      break;
    }
  case InstrType::VectorInstr:
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
  case InstrType::AtomicRMW:
  case InstrType::Fence:
    return false;
  case InstrType::BinaryInstr:
    switch (static_cast<BinaryInstrSubType>(subtype)) {
    case BinaryInstrSubType::INVALID:
    case BinaryInstrSubType::IntAdd:
    case BinaryInstrSubType::IntMul:
    case BinaryInstrSubType::Or:
    case BinaryInstrSubType::Xor:
    case BinaryInstrSubType::And:
    case BinaryInstrSubType::FloatAdd:
    case BinaryInstrSubType::FloatMul:
      return true;
    case BinaryInstrSubType::PtrAdd:
    case BinaryInstrSubType::IntSub:
    case BinaryInstrSubType::IntSRem:
    case BinaryInstrSubType::IntURem:
    case BinaryInstrSubType::IntSDiv:
    case BinaryInstrSubType::IntUDiv:
    case BinaryInstrSubType::FloatSub:
    case BinaryInstrSubType::FloatDiv:
    case BinaryInstrSubType::Shl:
    case BinaryInstrSubType::Shr:
    case BinaryInstrSubType::AShr:
      return false;
    }
  case InstrType::Intrinsic:
    switch (static_cast<IntrinsicSubType>(subtype)) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTLZ:
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
    case IntrinsicSubType::Abs:
    case IntrinsicSubType::FAbs:
    case IntrinsicSubType::IsConstant:
    case IntrinsicSubType::PopCnt:
    case IntrinsicSubType::FRound:
    case IntrinsicSubType::FFloor:
    case IntrinsicSubType::FCeil:
    case IntrinsicSubType::FTrunc:
    case IntrinsicSubType::CTTZ:
    case fir::IntrinsicSubType::Memset:
    case fir::IntrinsicSubType::Memcpy:
      return false;
    case IntrinsicSubType::UMin:
    case IntrinsicSubType::UMax:
    case IntrinsicSubType::SMin:
    case IntrinsicSubType::SMax:
    case IntrinsicSubType::FMin:
    case IntrinsicSubType::FMax:
    case IntrinsicSubType::FMinimum:
    case IntrinsicSubType::FMaximum:
      return true;
    }
  case InstrType::FCmp:
    switch (static_cast<FCmpInstrSubType>(subtype)) {
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
    switch (static_cast<ICmpInstrSubType>(subtype)) {
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
  case InstrType::VectorInstr:
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
      return !f->attribs.mem_read_none && !f->attribs.mem_read_only;
    }
    return true;
    // TODO: not sure fences kinda dont do this but can kinda kinda lead to
    // stuff being "updated" afterwards
  case InstrType::Fence:
  case InstrType::AtomicRMW:
  case InstrType::StoreInstr:
    return true;
  case InstrType::Intrinsic:
    switch (static_cast<IntrinsicSubType>(subtype)) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTTZ:
    case IntrinsicSubType::CTLZ:
    case IntrinsicSubType::Abs:
    case IntrinsicSubType::FAbs:
    case IntrinsicSubType::UMin:
    case IntrinsicSubType::UMax:
    case IntrinsicSubType::SMin:
    case IntrinsicSubType::SMax:
    case IntrinsicSubType::FMin:
    case IntrinsicSubType::FMax:
    case IntrinsicSubType::FMinimum:
    case IntrinsicSubType::FMaximum:
    case IntrinsicSubType::IsConstant:
    case IntrinsicSubType::PopCnt:
    case IntrinsicSubType::FRound:
    case IntrinsicSubType::FFloor:
    case IntrinsicSubType::FCeil:
    case IntrinsicSubType::FTrunc:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
    case fir::IntrinsicSubType::Memset:
    case fir::IntrinsicSubType::Memcpy:
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
  case InstrType::VectorInstr:
    return false;
  }
}

bool InstrData::pot_reads_mem() const {
  switch (instr_type) {
  case InstrType::CallInstr:
    if (args[0].is_constant() && args[0].as_constant()->is_func()) {
      auto f = args[0].as_constant()->as_func();
      return !f->attribs.mem_read_none;
    }
    return true;
  case InstrType::Intrinsic:
    switch (static_cast<IntrinsicSubType>(subtype)) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTTZ:
    case IntrinsicSubType::CTLZ:
    case IntrinsicSubType::Abs:
    case IntrinsicSubType::FAbs:
    case IntrinsicSubType::UMin:
    case IntrinsicSubType::UMax:
    case IntrinsicSubType::SMin:
    case IntrinsicSubType::SMax:
    case IntrinsicSubType::FMin:
    case IntrinsicSubType::FMax:
    case IntrinsicSubType::FMinimum:
    case IntrinsicSubType::FMaximum:
    case IntrinsicSubType::IsConstant:
    case IntrinsicSubType::PopCnt:
    case IntrinsicSubType::FRound:
    case IntrinsicSubType::FFloor:
    case IntrinsicSubType::FCeil:
    case IntrinsicSubType::FTrunc:
    case fir::IntrinsicSubType::Memset:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
    case fir::IntrinsicSubType::Memcpy:
      return true;
    }
    // TODO: not sure fences kinda dont do this but can kinda kinda lead to
    // stuff needing to be there to be "read" afterwards
  case InstrType::Fence:
  case InstrType::AtomicRMW:
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
  case InstrType::VectorInstr:
    return false;
  }
}

bool InstrData::has_pot_sideeffects() const {
  if (instr_type == InstrType::LoadInstr && Volatile) {
    return true;
  }
  switch (instr_type) {
  case InstrType::CallInstr:
  case InstrType::StoreInstr:
  case InstrType::AllocaInstr:
  case InstrType::AtomicRMW:
  case InstrType::Fence:
    return true;
  case InstrType::Intrinsic:
    switch (static_cast<IntrinsicSubType>(subtype)) {
    case IntrinsicSubType::INVALID:
    case IntrinsicSubType::CTTZ:
    case IntrinsicSubType::CTLZ:
    case IntrinsicSubType::Abs:
    case IntrinsicSubType::FAbs:
    case IntrinsicSubType::UMin:
    case IntrinsicSubType::UMax:
    case IntrinsicSubType::SMin:
    case IntrinsicSubType::SMax:
    case IntrinsicSubType::FMin:
    case IntrinsicSubType::FMax:
    case IntrinsicSubType::FMinimum:
    case IntrinsicSubType::FMaximum:
    case IntrinsicSubType::IsConstant:
    case IntrinsicSubType::PopCnt:
    case IntrinsicSubType::FRound:
    case IntrinsicSubType::FFloor:
    case IntrinsicSubType::FCeil:
    case IntrinsicSubType::FTrunc:
      return false;
    case IntrinsicSubType::VA_start:
    case IntrinsicSubType::VA_end:
    case fir::IntrinsicSubType::Memset:
    case fir::IntrinsicSubType::Memcpy:
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
  case InstrType::VectorInstr:
    return false;
  }
}

InstrData InstrData::get_call(TypeR ty) {
  auto res =
      InstrData{InstrType::CallInstr, ty, BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_float_add(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::FloatAdd), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_float_sub(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::FloatSub), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_float_mul(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::FloatMul), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_float_div(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::FloatDiv), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_add(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::IntAdd), ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_smod(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::IntSRem), ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_umod(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::IntURem), ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_intrinsic(TypeR ty, IntrinsicSubType sub_type) {
  auto res = InstrData{InstrType::Intrinsic, static_cast<u32>(sub_type), ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_vector(TypeR ty, VectorISubType sub_type) {
  auto res = InstrData{InstrType::VectorInstr, static_cast<u32>(sub_type), ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_binary(TypeR ty, BinaryInstrSubType sub_type) {
  auto res = InstrData{InstrType::BinaryInstr, static_cast<u32>(sub_type), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_unary(TypeR ty, UnaryInstrSubType sub_type) {
  auto res = InstrData{InstrType::UnaryInstr, static_cast<u32>(sub_type), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_conversion(TypeR ty, ConversionSubType sub_type) {
  auto res = InstrData{InstrType::Conversion, static_cast<u32>(sub_type), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_mul(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::IntMul), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_shl(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::Shl), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_ashr(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::AShr), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_lshr(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::Shr), ty,
                       BasicBlock(BasicBlock::invalid())};
  return res;
}

InstrData InstrData::get_sub(TypeR ty) {
  auto res = InstrData{InstrType::BinaryInstr,
                       static_cast<u32>(BinaryInstrSubType::IntSub), ty,
                       BasicBlock(BasicBlock::invalid())};
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

InstrData InstrData::get_atomic_rmw(TypeR ty, AtomicRMWSubType sub_type) {
  auto res = InstrData{InstrType::AtomicRMW, static_cast<u32>(sub_type), ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  return res;
}

InstrData InstrData::get_fence(TypeR ty) {
  auto res =
      InstrData{InstrType::Fence, 0, ty, BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(1);
  return res;
}

InstrData InstrData::get_int_cmp(TypeR ty, ICmpInstrSubType cmp_ty) {
  auto res = InstrData{InstrType::ICmp, static_cast<u32>(cmp_ty), ty,
                       BasicBlock(BasicBlock::invalid())};
  // res.args.reserve(2);
  return res;
}

InstrData InstrData::get_float_cmp(TypeR ty, FCmpInstrSubType cmp_ty) {
  auto res = InstrData{InstrType::FCmp, static_cast<u32>(cmp_ty), ty,
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
