#pragma once
#include "ir/attributable.hpp"
#include "ir/basic_block_ref.hpp"
#include "use.hpp"
#include "utils/todo.hpp"
#include "utils/vec.hpp"
#include "value.hpp"

namespace foptim::fir {

enum class InstrType : u8 {
  ICmp,
  FCmp,
  BinaryInstr,
  UnaryInstr,
  AllocaInstr,

  ExtractValue,
  InsertValue,

  ITrunc,
  ZExt,
  SExt,
  Conversion,

  SelectInstr,

  CallInstr,

  // Terminators
  ReturnInstr,
  BranchInstr,
  CondBranchInstr,
  // Unreachable,

  // Memory
  LoadInstr,
  StoreInstr,
};

struct BBRefWithArgs {
  BasicBlock bb;
  IRVec<ValueR> args;

  constexpr bool operator==(const BBRefWithArgs &other) const {
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
};

enum class ConversionSubType : u32 {
  INVALID = 0,
  FPEXT,
  FPTRUNC,
  FPTOUI,
  FPTOSI,
  UITOFP,
  SITOFP,
  PtrToInt,
  IntToPtr,
};

enum class ICmpInstrSubType : u32 {
  INVALID = 0,
  SLT,
  ULT,
  NE,
  EQ,
  SGT,
  UGT,
  UGE,
  ULE,
  SGE,
  SLE,

  MulOverflow,
  AddOverflow,
};

enum class FCmpInstrSubType : u32 {
  INVALID = 0,
  AlwFalse,
  OEQ,
  OGT,
  OGE,
  OLT,
  OLE,
  ONE,
  ORD,
  UNO,
  UEQ,
  UGT,
  UGE,
  ULT,
  ULE,
  UNE,
  AlwTrue,

  IsNaN,
};

enum class UnaryInstrSubType : u32 {
  INVALID = 0,
  FloatNeg,
  IntNeg,
  Not,
};

enum class BinaryInstrSubType : u32 {
  INVALID = 0,
  IntAdd,
  IntSub,
  IntMul,
  IntSRem,
  IntSDiv,
  IntUDiv,

  Shl,
  Shr,
  AShr,
  And,
  Or,
  Xor,

  FloatAdd,
  FloatSub,
  FloatMul,
  FloatDiv,
};

class InstrData : public Used, public Attributable {
public:
  InstrType instr_type;
  u32 subtype;
  TypeR value_type;
  InstrData(InstrType ty, TypeR vty, BasicBlock parent)
      : instr_type(ty), subtype(0), value_type(vty), parent(parent) {}
  InstrData(InstrType ty, u32 subtype, TypeR vty, BasicBlock parent)
      : instr_type(ty), subtype(subtype), value_type(vty), parent(parent) {}

  using Used::add_usage;
  IRVec<ValueR> args;
  IRVec<BBRefWithArgs> bbs;
  BasicBlock parent;

  [[nodiscard]] constexpr InstrType get_instr_type() const {
    return instr_type;
  }
  [[nodiscard]] constexpr u32 get_instr_subtype() const { return subtype; }
  constexpr void set_parent(BasicBlock new_parent) { parent = new_parent; }
  [[nodiscard]] constexpr BasicBlock get_parent() const { return parent; }

  [[nodiscard]] constexpr const auto &get_uses() const { return uses; }
  [[nodiscard]] constexpr const auto &get_args() const { return args; }
  [[nodiscard]] constexpr const ValueR &get_arg(size_t indx) const {
    return args.at(indx);
  }
  [[nodiscard]] constexpr bool has_args() const { return !args.empty(); }
  [[nodiscard]] constexpr const auto &get_bb_args() const { return bbs; }
  [[nodiscard]] constexpr TypeR get_type() const { return value_type; }
  [[nodiscard]] bool verify(const BasicBlockData *) const;
  [[nodiscard]] bool has_result() const;
  [[nodiscard]] bool is_critical() const;
  /*true if the instructions arguments can be swapped without changing the
   * result*/
  [[nodiscard]] bool is_commutative() const;
  [[nodiscard]] bool pot_modifies_mem() const;
  [[nodiscard]] bool has_pot_sideeffects() const;

  [[nodiscard]] constexpr const char *get_name() const {
    switch (instr_type) {
    case InstrType::UnaryInstr:
      switch ((UnaryInstrSubType)subtype) {
      case UnaryInstrSubType::INVALID:
        return "UNARYYOP_INVALID";
      case UnaryInstrSubType::FloatNeg:
        return "FloatNeg";
      case UnaryInstrSubType::IntNeg:
        return "IntNeg";
      case UnaryInstrSubType::Not:
        return "Not";
      }
    case InstrType::BinaryInstr:
      switch ((BinaryInstrSubType)subtype) {
      case BinaryInstrSubType::INVALID:
        return "BINARYOP_INVALID";
      case BinaryInstrSubType::IntAdd:
        return "IntAdd";
      case BinaryInstrSubType::IntSub:
        return "IntSub";
      case BinaryInstrSubType::IntMul:
        return "IntMul";
      case BinaryInstrSubType::IntSRem:
        return "IntSRem";
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
      switch ((ConversionSubType)subtype) {
      case ConversionSubType::INVALID:
        return "CONVERSIONOP_INVALID";
      case ConversionSubType::FPTOUI:
        return "FP_UI";
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
      switch ((ICmpInstrSubType)subtype) {
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
      switch ((FCmpInstrSubType)subtype) {
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

  [[nodiscard]] constexpr bool is(InstrType ty) const {
    return ty == instr_type;
  }

  constexpr void verify() const {
    switch (instr_type) {
    case InstrType::BinaryInstr:
      ASSERT(args.size() == 2);
      break;
    case InstrType::ICmp:
      ASSERT(args.size() == 2);
      break;
    case InstrType::CallInstr:
      ASSERT(args.size() >= 1);
      break;
    case InstrType::AllocaInstr:
      ASSERT(args.size() == 1);
      break;
    case InstrType::ReturnInstr:
      ASSERT(args.size() <= 1);
      break;
    case InstrType::LoadInstr:
      ASSERT(args.size() == 1);
      break;
    case InstrType::StoreInstr:
      ASSERT(args.size() == 2);
      break;
    default:
      break;
    }
  }

  static InstrData get_extract_value(TypeR ty);
  static InstrData get_insert_value(TypeR ty);
  static InstrData get_smod(TypeR ty);
  static InstrData get_add(TypeR ty);
  static InstrData get_sub(TypeR ty);
  static InstrData get_mul(TypeR ty);
  static InstrData get_shl(TypeR ty);
  static InstrData get_ashr(TypeR ty);
  static InstrData get_lshr(TypeR ty);
  static InstrData get_binary(TypeR ty, BinaryInstrSubType sub_type);
  static InstrData get_unary(TypeR ty, UnaryInstrSubType sub_type);
  static InstrData get_conversion(TypeR ty, ConversionSubType sub_type);
  static InstrData get_float_add(TypeR ty);
  static InstrData get_float_sub(TypeR ty);
  static InstrData get_float_mul(TypeR ty);
  static InstrData get_float_div(TypeR ty);
  static InstrData get_sext(TypeR ty);
  static InstrData get_itrunc(TypeR ty);
  static InstrData get_zext(TypeR ty);
  static InstrData get_int_cmp(TypeR ty, ICmpInstrSubType cmp_ty);
  static InstrData get_float_cmp(TypeR ty, FCmpInstrSubType cmp_ty);
  static InstrData get_return(TypeR ty);
  static InstrData get_call(TypeR ty);
  static InstrData get_alloca(TypeR ty);
  static InstrData get_load(TypeR ty);
  static InstrData get_select(TypeR ty);
  static InstrData get_store(TypeR ty);
  static InstrData get_branch(ContextData *ctx);
  static InstrData get_cond_branch(ContextData *ctx);

  [[nodiscard]] bool eql_expr(const InstrData &other) const {
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
};

} // namespace foptim::fir
